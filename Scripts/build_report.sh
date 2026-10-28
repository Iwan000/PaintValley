#!/bin/zsh
# Project Rain - Docs/<name>.md -> Docs/<name>.pdf (pandoc to HTML, headless Chrome to PDF; no LaTeX needed). Prints the
# page count; the brief allows 2 pages.
#   Scripts/build_report.sh                     # Docs/PerformanceReport (Rain Shield)
#   Scripts/build_report.sh PaintValleyReport   # Docs/PaintValleyReport (Paint Valley)
NAME=${1:-PerformanceReport}
cd /Users/guocheng/ProjectRain/Docs
pandoc $NAME.md -s --embed-resources --css=report.css -o $NAME.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --no-pdf-header-footer \
  --print-to-pdf=$NAME.pdf "file://$PWD/$NAME.html" 2>/dev/null
python3 -c "import re; d=open('$NAME.pdf','rb').read(); print('pages:', len(re.findall(rb'/Type\s*/Page[^s]', d)))"
