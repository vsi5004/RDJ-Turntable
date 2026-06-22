#!/usr/bin/env python3
"""pdfsearch.py - extract / search text from a PDF (datasheets, app notes).

Requires: pip install --user pymupdf

Usage:
  python tools/pdfsearch.py <file.pdf>                 # page count + chars per page
  python tools/pdfsearch.py <file.pdf> term [term2..]  # print pages matching ANY term
  python tools/pdfsearch.py <file.pdf> --pages 10-14   # dump text of a page range
"""
import sys
import fitz  # PyMuPDF


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    doc = fitz.open(args[0])
    rest = args[1:]

    if rest and rest[0] == "--pages":
        lo, _, hi = rest[1].partition("-")
        hi = hi or lo
        for p in range(int(lo) - 1, int(hi)):
            print(f"\n===== page {p + 1} =====")
            print(doc[p].get_text())
        return

    if not rest:
        print(f"{doc.page_count} pages")
        for i, pg in enumerate(doc):
            print(f"page {i + 1}: {len(pg.get_text())} chars")
        return

    terms = [t.lower() for t in rest]
    for i, pg in enumerate(doc):
        text = pg.get_text()
        if any(t in text.lower() for t in terms):
            print(f"\n===== page {i + 1} (match) =====")
            print(text)


if __name__ == "__main__":
    main()
