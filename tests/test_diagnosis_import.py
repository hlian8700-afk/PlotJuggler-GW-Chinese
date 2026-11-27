import importlib.util,io,json,pathlib,sqlite3,tempfile,unittest,zipfile
MODULE=pathlib.Path(__file__).parents[1]/"tools"/"import_diagnosis_config.py"
spec=importlib.util.spec_from_file_location("importer",MODULE);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def workbook():
    output=io.BytesIO()
    with zipfile.ZipFile(output,"w") as z:
        z.writestr("xl/workbook.xml",'<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>')
        z.writestr("xl/_rels/workbook.xml.rels",'<Relationships><Relationship Id="rId1" Target="worksheets/sheet1.xml"/></Relationships>')
        z.writestr("xl/worksheets/sheet1.xml",'<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1:C1039074"/><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>故障代码</t></is></c><c r="B1" t="inlineStr"><is><t>中文含义</t></is></c></row><row r="2"><c r="A2" t="inlineStr"><is><t>SpeedFault</t></is></c><c r="B2" t="inlineStr"><is><t>超速</t></is></c><c r="C2"><f>ROW()-1</f><v>1</v></c></row><row r="1039074"><c r="A1039074" s="1"/></row></sheetData></worksheet>')
    return output.getvalue()

class ImportTests(unittest.TestCase):
    def test_sparse_worksheet(self):
        rows=list(m.excel_rows(workbook()))
        self.assertEqual(len(rows),2);self.assertEqual(rows[1][1],2)
        self.assertEqual(rows[1][2][2],"=ROW()-1")

    def test_audited_draft_import(self):
        with tempfile.TemporaryDirectory() as folder:
            root=pathlib.Path(folder);archive=root/"config.zip";output=root/"rules.sqlite"
            with zipfile.ZipFile(archive,"w") as z:
                z.writestr("配置表/超速故障代码.xlsx",workbook())
                z.writestr("配置表/tag_info.xlsx",workbook())
                z.writestr("配置表/限位开关误触发故障代码.csv","error_code,pitch_axis,pitch_position,delay_time\nLimit,1,91,3\n".encode("utf-8-sig"))
            count=m.import_package(archive,output)
            self.assertEqual(count,3)
            with sqlite3.connect(output) as db:
                self.assertEqual(db.execute("SELECT count(*) FROM records WHERE state!='draft'").fetchone()[0],0)
                self.assertTrue(db.execute("SELECT count(*) FROM issues WHERE kind='missing_file'").fetchone()[0]>0)
                self.assertEqual(db.execute("SELECT row_number FROM records WHERE source='超速故障代码.xlsx'").fetchone()[0],2)
            with self.assertRaises(ValueError):m.import_package(archive,output)

    def test_duplicate_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=pathlib.Path(folder);archive=root/"config.zip";output=root/"rules.sqlite"
            with zipfile.ZipFile(archive,"w") as z:
                z.writestr("a/error_list.xlsx",workbook());z.writestr("b/error_list.xlsx",workbook())
            with self.assertRaises(ValueError):m.import_package(archive,output)
            self.assertFalse(output.exists())

    def test_csv_newlines(self):
        rows=list(m.csv_rows('code,note\nx,"line1\nline2"\n'.encode("utf-8-sig")))
        self.assertEqual(rows[1][2][1],"line1\nline2")
if __name__=="__main__":unittest.main()
