#!/bin/zsh
# Project Rain - Docs/PerformanceReport.md -> Docs/PerformanceReport.pdf (pandoc to HTML, headless Chrome to PDF;
# no LaTeX needed). Prints the page count; the brief allows 2 pages.
cd /Users/guocheng/ProjectRain/Docs
pandoc PerformanceReport.md -s --embed-resources --css=report.css -o PerformanceReport.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --no-pdf-header-footer \
  --print-to-pdf=PerformanceReport.pdf "file://$PWD/PerformanceReport.html" 2>/dev/null
python3 -c "import re; d=open('PerformanceReport.pdf','rb').read(); print('pages:', len(re.findall(rb'/Type\s*/Page[^s]', d)))"
