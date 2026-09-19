import re
import zipfile
import pathlib
import sys

docx = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
with zipfile.ZipFile(docx) as z:
    xml = z.read("word/document.xml").decode("utf-8")
text = re.sub(r"</w:p>", "\n", xml)
text = re.sub(r"<[^>]+>", "", text)
for a, b in (
    ("&lt;", "<"),
    ("&gt;", ">"),
    ("&amp;", "&"),
    ("&#x0D;", ""),
    ("&quot;", '"'),
):
    text = text.replace(a, b)
text = re.sub(r"\n{3,}", "\n\n", text).strip()
out.write_text(text, encoding="utf-8")
print(f"wrote {out} ({len(text)} chars)")
