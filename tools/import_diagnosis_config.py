#!/usr/bin/env python3
"""Import external configuration into a local, audited draft SQLite rule package.
No configuration is executed, uploaded, or marked validated by this importer.
"""
import argparse, csv, hashlib, io, json, os, pathlib, posixpath, sqlite3, tempfile, zipfile
import xml.etree.ElementTree as ET

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
EXPECTED = {
 "error_list.xlsx","pitch_tag_infoV1.2.xlsx","tag_info.xlsx",
 "tracelog故障代码对照表.csv","打图配置表.xlsx","振动停机故障代码配置表.xlsx",
 "振动类型及解决方案.xlsx","故障特征识别规则库V1.1.xlsx","机组信息配置表.csv",
 "综合诊断规则库.xlsx","超速故障代码.xlsx","超速类型及解决方案.xlsx",
 "转速震荡类型及解决方案.xlsx","限位开关误触发故障代码.csv",
 "项目机型_额定转速v2.csv","首触故障代码库(变桨)V1.5.csv","首触故障代码库V1.5.csv"
}
def csv_rows(data):
    text = None
    for encoding in ("utf-8-sig","gb18030","utf-16"):
        try: text=data.decode(encoding);break
        except UnicodeError: pass
    if text is None: raise ValueError("CSV encoding is unsupported")
    for index,row in enumerate(csv.reader(io.StringIO(text,newline="")),1):
        if any(value.strip() for value in row):yield "CSV", index, row

def excel_rows(data):
    # Uses actual XML cells, never worksheet.dimension/max_row or formula evaluation.
    with zipfile.ZipFile(io.BytesIO(data)) as book:
        if sum(item.file_size for item in book.infolist())>256*1024*1024:
            raise ValueError("Uncompressed workbook exceeds 256 MiB")
        strings=[]
        if "xl/sharedStrings.xml" in book.namelist():
            with book.open("xl/sharedStrings.xml") as stream:
                for event,node in ET.iterparse(stream,events=("end",)):
                    if node.tag==NS+"si":
                        strings.append("".join(part.text or "" for part in node.iter(NS+"t")));node.clear()
        workbook=ET.fromstring(book.read("xl/workbook.xml"))
        rels=ET.fromstring(book.read("xl/_rels/workbook.xml.rels"))
        targets={node.attrib["Id"]:node.attrib["Target"] for node in rels}
        sheets=workbook.find(NS+"sheets")
        for sheet in sheets:
            rid=sheet.attrib["{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"]
            target=targets[rid]
            target=posixpath.normpath(target.lstrip("/") if target.startswith("/") else "xl/"+target)
            if not target.startswith("xl/"):raise ValueError("Workbook relationship leaves xl/")
            with book.open(target) as stream:
                data_root=None
                for event,node in ET.iterparse(stream,events=("start","end")):
                    if event=="start" and node.tag==NS+"sheetData":data_root=node
                    if event!="end" or node.tag!=NS+"row":continue
                    cells={}
                    for cell in node.findall(NS+"c"):
                        letters="".join(c for c in cell.attrib.get("r","") if c.isalpha())
                        col=0
                        for char in letters:col=col*26+ord(char.upper())-64
                        if not col:raise ValueError("Cell reference missing")
                        formula=cell.find(NS+"f")
                        if formula is not None:value="="+(formula.text or "")
                        elif cell.attrib.get("t")=="inlineStr":value="".join(p.text or "" for p in cell.iter(NS+"t"))
                        else:
                            value=cell.findtext(NS+"v","")
                            if cell.attrib.get("t")=="s" and value:value=strings[int(value)]
                        if value!="":cells[col-1]=value
                    if cells:
                        values=[""]*(max(cells)+1)
                        for col,value in cells.items():values[col]=value
                        yield sheet.attrib["name"],int(node.attrib.get("r","0")),values
                    # Detach completed rows: formatted million-row worksheets stay bounded.
                    node.clear()
                    if data_root is not None:data_root.remove(node)

def headers(values):
    counts={};out=[]
    for index,value in enumerate(values):
        value=value.strip() or "_unnamed_"+str(index+1)
        counts[value]=counts.get(value,0)+1
        out.append(value if counts[value]==1 else value+"__"+str(counts[value]))
    return out

def create_schema(db):
    db.executescript("""
    CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
    CREATE TABLE sources(name TEXT PRIMARY KEY,sha256 TEXT NOT NULL,encoding TEXT);
    CREATE TABLE records(source TEXT,sheet TEXT,row_number INTEGER,payload TEXT NOT NULL,
      state TEXT NOT NULL DEFAULT 'draft',PRIMARY KEY(source,sheet,row_number));
    CREATE TABLE issues(source TEXT,sheet TEXT,row_number INTEGER,kind TEXT,message TEXT);
    CREATE TABLE aliases(source TEXT,sheet TEXT,row_number INTEGER,role TEXT,alias TEXT);
    CREATE INDEX aliases_lookup ON aliases(role,alias);
    CREATE TABLE codes(source TEXT,sheet TEXT,row_number INTEGER,model TEXT,code TEXT,
      category TEXT,description TEXT);
    CREATE INDEX code_lookup ON codes(model,code);
    """)

def import_package(archive,output):
    output=pathlib.Path(output)
    if output.exists():raise ValueError("Output already exists; use a new package filename")
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,temporary=tempfile.mkstemp(prefix=".diagnosis-",suffix=".sqlite",dir=output.parent);os.close(fd)
    db=None
    try:
        db=sqlite3.connect(temporary);create_schema(db)
        db.execute("INSERT INTO metadata VALUES(?,?)",("schema_version","1"))
        db.execute("INSERT INTO metadata VALUES(?,?)",("validation_state","draft"))
        with zipfile.ZipFile(archive) as package:
            if sum(x.file_size for x in package.infolist())>512*1024*1024:
                raise ValueError("Uncompressed package exceeds 512 MiB")
            members={}
            for item in package.infolist():
                if item.is_dir():continue
                name=pathlib.PurePosixPath(item.filename.replace("\\","/")).name
                if name not in EXPECTED:continue
                if name in members:raise ValueError("Duplicate configuration basename: "+name)
                members[name]=item
            for name in sorted(EXPECTED-set(members)):
                db.execute("INSERT INTO issues VALUES(?,?,?,?,?)",(name,"",0,"missing_file","Expected configuration file is missing"))
            for name,item in sorted(members.items()):
                data=package.read(item);db.execute("INSERT INTO sources VALUES(?,?,?)",
                  (name,hashlib.sha256(data).hexdigest(),"xlsx" if name.endswith(".xlsx") else "auto"))
                header_by_sheet={}
                iterator=excel_rows(data) if name.endswith(".xlsx") else csv_rows(data)
                for sheet,row,values in iterator:
                    if sheet not in header_by_sheet:
                        header_by_sheet[sheet]=headers(values);continue
                    keys=header_by_sheet[sheet]
                    keys=keys+["_unnamed_"+str(i+1) for i in range(len(keys),len(values))]
                    record=dict(zip(keys,values+[""]*max(0,len(keys)-len(values))))
                    db.execute("INSERT INTO records(source,sheet,row_number,payload) VALUES(?,?,?,?)",
                        (name,sheet,row,json.dumps(record,ensure_ascii=False)))
                    def issue(kind,message):
                        db.execute("INSERT INTO issues VALUES(?,?,?,?,?)",(name,sheet,row,kind,message))
                    if any(key.startswith("_unnamed_") and str(value).strip() for key,value in record.items()):
                        issue("unnamed_column","Nonempty unnamed column retained")
                    if any(str(v).startswith("=") for v in values):issue("formula","Formula retained as text, never evaluated")
                    if name in ("tag_info.xlsx","pitch_tag_infoV1.2.xlsx"):
                        role=str(record.get("tags","")).strip()
                        for key,value in record.items():
                            if not key.startswith("config"):continue
                            value=str(value).strip()
                            if not value or value=="1":continue
                            db.execute("INSERT INTO aliases VALUES(?,?,?,?,?)",(name,sheet,row,role,value))
                    code=record.get("故障代码英文1") or record.get("故障代码") or record.get("error_code")
                    if code:
                        db.execute("INSERT INTO codes VALUES(?,?,?,?,?,?,?)",
                          (name,sheet,row,str(record.get("机型","")),str(code).strip(),
                           str(record.get("故障分类","")),str(record.get("故障代码中文") or record.get("中文含义",""))))
                    priority=record.get("代码特征优先级")
                    if priority:issue("unresolved_priority","Legacy priority requires reviewed mapping: "+str(priority))
                    if name=="项目机型_额定转速v2.csv":
                        for key in ("叶轮额定转速","发电机额定转速"):
                            try:
                                value=float(record.get(key,""))
                                if not (0<value<float("inf")):raise ValueError()
                            except (ValueError,TypeError):issue("invalid_rated_speed",key+" must be confirmed before use")
            # Retain all variants, flag conflicts; never silently overwrite a source.
            conflicts=db.execute("""SELECT model,code FROM codes GROUP BY model,code
              HAVING COUNT(DISTINCT category||char(31)||description)>1""").fetchall()
            for model,code in conflicts:
                db.execute("INSERT INTO issues VALUES(?,?,?,?,?)",("", "",0,"code_conflict",
                  json.dumps({"model":model,"code":code},ensure_ascii=False)))
        count=db.execute("SELECT count(*) FROM records").fetchone()[0]
        db.commit();db.close();db=None;os.replace(temporary,output)
        return count
    except BaseException:
        if db is not None:db.close()
        pathlib.Path(temporary).unlink(missing_ok=True)
        raise

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive");parser.add_argument("--output",required=True)
    args=parser.parse_args()
    print("Imported draft rows:",import_package(args.archive,args.output))
