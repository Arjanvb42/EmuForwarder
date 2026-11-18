from pathlib import Path
from xml.etree import ElementTree as ET

project = Path("project.gp4")
tree = ET.parse(project)
root = tree.getroot()
files = root.find("files")
directory = root.find("rootdir")
files[:] = []
directory[:] = []

paths = {}
for path in sorted(Path(".").rglob("*")):
    if path == project or path.parts[0] == ".build":
        continue
    if path.is_dir():
        parent = directory if path.parent == Path(".") else paths[path.parent]
        paths[path] = ET.SubElement(parent, "dir", targ_name=path.name)
    elif path.is_file():
        ET.SubElement(files, "file", targ_path=path.as_posix(), orig_path=path.as_posix())

tree.write(project, encoding="utf-8", xml_declaration=True)
