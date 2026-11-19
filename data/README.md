# Mapping file required by the Windows build

Place the supplied UTF-8 CSV at `data/gw_tag_mapping.csv`.

Expected columns: `raw_tag,cn_name`. The file must contain all 1,981 original mappings. The build copies it next to `plotjuggler.exe`; the application reads it at runtime. Raw series identifiers remain unchanged.
