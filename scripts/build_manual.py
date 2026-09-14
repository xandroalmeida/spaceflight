#!/usr/bin/env python3
"""Compila o manual do usuário: docs/manual/*.md -> HTML -> PDF.

    ./scripts/build_manual.sh          # regenera as teclas e compila tudo

Sem dependências. Não há pandoc, LaTeX, wkhtmltopdf nem um módulo de Markdown
nesta máquina, e instalar um deles para compilar um documento de quinze páginas
custaria mais manutenção do que o renderizador de Markdown que está aqui -- que
cobre o subconjunto que o manual usa e nada mais. O PDF sai do Chrome, que é o
único motor de impressão instalado.

## O que este script existe para impedir

Um manual envelhece de três maneiras, e as três dão erro de compilação aqui:

  {{key:acao}}        resolvido a partir de docs/gameplay/controls.json, que sai
                      do Input Map. Uma ação que não existe mais reprova a
                      compilação em vez de imprimir uma tecla errada.

  ![](figura.png)     uma figura que não existe reprova. As capturas vêm de
                      scripts/m7_screenshots.sh e são reproduzíveis; um manual
                      com buracos seria pior do que nenhum.

  {{include:ficheiro}} texto gerado noutro lugar entra por referência. A tabela
                      completa de teclas tem UMA fonte, e o manual aponta para
                      ela em vez de a copiar.

  {{anchor:figura:peca}} onde a peça caiu NA imagem, em percentagem do quadro.
                      Quem calcula é a câmera que tirou a fotografia
                      (`ShotDirector._write_anchors`), e o valor chega aqui num
                      `.anchors.json` ao lado do PNG. Marcadores escritos à mão
                      envelheciam em silêncio: bastava mudar o enquadramento
                      para o número "motor principal" passar a apontar para um
                      radiador, sem que nada falhasse.
"""

from __future__ import annotations

import html
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANUAL = ROOT / "docs/manual"
SHOTS = ROOT / "docs/validation/m7"
CONTROLS = ROOT / "docs/gameplay/controls.json"
BUILD = ROOT / "build/manual"
OUTPUT_PDF = MANUAL / "manual.pdf"

CHROME_CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
]


class BuildError(Exception):
    pass


# --- substituições verificadas -------------------------------------------

def load_keys() -> dict:
    if not CONTROLS.exists():
        raise BuildError(f"falta {CONTROLS.relative_to(ROOT)} -- rode scripts/dump_controls.sh")
    return json.loads(CONTROLS.read_text())


def expand(text: str, source: pathlib.Path, keys: dict, seen: set) -> str:
    """Resolve {{key:...}} e {{include:...}}, com erro em vez de silêncio."""
    def key(match):
        action = match.group(1)
        if action not in keys:
            raise BuildError(f"{source.name}: {{{{key:{action}}}}} -- ação não existe "
                             f"no Input Map ({CONTROLS.relative_to(ROOT)})")
        # A tecla NUA, sem marcação. Quem escreve o capítulo é que decide se ela
        # vai entre crases -- porque dentro de um bloco de código as crases são
        # literais, e a primeira versão imprimia `Tab` com elas à vista.
        return keys[action]["key"]

    def anchor(match):
        shot, part = match.group(1), match.group(2)
        path = SHOTS / f"{shot}.anchors.json"
        if not path.exists():
            raise BuildError(f"{source.name}: {{{{anchor:{shot}:{part}}}}} -- falta "
                             f"{path.relative_to(ROOT)}; rode scripts/m7_screenshots.sh")
        places = json.loads(path.read_text())
        if part not in places:
            raise BuildError(f"{source.name}: {{{{anchor:{shot}:{part}}}}} -- a captura só "
                             f"traz {', '.join(sorted(places))}")
        at = places[part]
        return f"--x:{at['x']};--y:{at['y']}"

    def include(match):
        target = (source.parent / match.group(1)).resolve()
        if not target.exists():
            raise BuildError(f"{source.name}: {{{{include:{match.group(1)}}}}} não existe")
        if target in seen:
            raise BuildError(f"{source.name}: include circular em {match.group(1)}")
        body = target.read_text()
        # O primeiro título do arquivo incluído sai: o capítulo já tem o seu.
        body = re.sub(r"\A#\s+.*\n", "", body)
        return expand(body, target, keys, seen | {target})

    text = re.sub(r"\{\{key:([a-z_]+)\}\}", key, text)
    text = re.sub(r"\{\{anchor:([a-z0-9-]+):([a-z_]+)\}\}", anchor, text)
    text = re.sub(r"\{\{include:([^}]+)\}\}", include, text)
    if "{{" in text:
        stray = re.search(r"\{\{[^}]*\}\}", text)
        raise BuildError(f"{source.name}: substituição desconhecida {stray.group(0)}")
    return text


# --- Markdown -> HTML ----------------------------------------------------

INLINE = [
    (re.compile(r"`([^`]+)`"), lambda m: f"<code>{html.escape(m.group(1))}</code>"),
    (re.compile(r"\*\*([^*]+)\*\*"), lambda m: f"<strong>{m.group(1)}</strong>"),
    (re.compile(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])"), lambda m: f"<em>{m.group(1)}</em>"),
]


def inline(text: str, images: list, source: pathlib.Path) -> str:
    # O código vem primeiro e é protegido: `**` dentro de `código` não é negrito.
    slots = []

    def stash(match):
        slots.append(f"<code>{html.escape(match.group(1))}</code>")
        return f"\x00{len(slots) - 1}\x00"

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)

    def image(match):
        alt, src = match.group(1), match.group(2)
        resolved = (source.parent / src).resolve()
        if not resolved.exists():
            raise BuildError(f"{source.name}: figura ausente -- {src}")
        images.append(resolved)
        return (f'<figure><img src="{resolved.as_uri()}" alt="{html.escape(alt)}">'
                f'<figcaption>{alt}</figcaption></figure>')

    text = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", image, text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", text)
    for i, slot in enumerate(slots):
        text = text.replace(f"\x00{i}\x00", slot)
    return text


def render(markdown: str, source: pathlib.Path, images: list) -> str:
    out = []
    lines = markdown.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]

        if line.startswith("```"):
            language = line[3:].strip()
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                body.append(lines[i])
                i += 1
            i += 1
            classes = f' class="lang-{language}"' if language else ""
            out.append(f"<pre{classes}><code>{html.escape(chr(10).join(body))}</code></pre>")
            continue

        # HTML cru passa direto: é assim que as figuras com chamadas numeradas
        # são escritas, sem inventar sintaxe nova de Markdown.
        if line.startswith("<") and not line.startswith("<br"):
            block = []
            while i < len(lines) and lines[i].strip() != "":
                block.append(lines[i])
                i += 1
            html_block = "\n".join(block)
            for match in re.finditer(r'src="([^"]+)"', html_block):
                resolved = (source.parent / match.group(1)).resolve()
                if not resolved.exists():
                    raise BuildError(f"{source.name}: figura ausente -- {match.group(1)}")
                images.append(resolved)
                html_block = html_block.replace(f'src="{match.group(1)}"',
                                                f'src="{resolved.as_uri()}"')
            out.append(html_block)
            continue

        if match := re.match(r"^(#{1,4})\s+(.*)$", line):
            level = len(match.group(1))
            text = inline(match.group(2), images, source)
            anchor = re.sub(r"[^a-z0-9]+", "-", match.group(2).lower()).strip("-")
            out.append(f'<h{level} id="{anchor}">{text}</h{level}>')
            i += 1
            continue

        if re.match(r"^---+\s*$", line):
            out.append("<hr>")
            i += 1
            continue

        if line.startswith("|"):
            table = []
            while i < len(lines) and lines[i].startswith("|"):
                table.append(lines[i])
                i += 1
            out.append(render_table(table, images, source))
            continue

        if re.match(r"^\s*[-*]\s+", line) or re.match(r"^\s*\d+\.\s+", line):
            ordered = bool(re.match(r"^\s*\d+\.\s+", line))
            items = []
            while i < len(lines) and (re.match(r"^\s*[-*]\s+", lines[i])
                                      or re.match(r"^\s*\d+\.\s+", lines[i])
                                      or (items and lines[i].startswith("  ")
                                          and lines[i].strip())):
                if re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
                    items.append(re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i]))
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            tag = "ol" if ordered else "ul"
            body = "".join(f"<li>{inline(item, images, source)}</li>" for item in items)
            out.append(f"<{tag}>{body}</{tag}>")
            continue

        if line.startswith(">"):
            quote = []
            while i < len(lines) and lines[i].startswith(">"):
                quote.append(lines[i].lstrip("> ").rstrip())
                i += 1
            out.append(f"<blockquote>{inline(' '.join(quote), images, source)}</blockquote>")
            continue

        if line.strip() == "":
            i += 1
            continue

        paragraph = []
        while i < len(lines) and lines[i].strip() and not re.match(
                r"^(#{1,4}\s|```|\||>|\s*[-*]\s|\s*\d+\.\s|---+\s*$|<)", lines[i]):
            paragraph.append(lines[i].strip())
            i += 1
        body = inline(" ".join(paragraph), images, source)
        # ⚠️ Um `<figure>` NÃO pode viver dentro de um `<p>`: o navegador fecha o
        # parágrafo antes dela e o `</p>` órfão sobra depois, com a margem toda.
        # Quando a figura terminava no pé de uma página, essa margem abria a
        # seguinte -- e o `page-break-before` do capítulo empurrava tudo outra
        # vez. Era daí que vinha a página em branco entre o capítulo 1 e o 2.
        if body.startswith("<figure") and body.endswith("</figure>"):
            out.append(body)
        else:
            out.append(f"<p>{body}</p>")
    return "\n".join(out)


def render_table(rows: list, images: list, source: pathlib.Path) -> str:
    def cells(row):
        return [c.strip() for c in row.strip().strip("|").split("|")]

    header = cells(rows[0])
    alignment = []
    body_start = 1
    if len(rows) > 1 and re.match(r"^\|[\s:\-|]+\|?\s*$", rows[1]):
        for spec in cells(rows[1]):
            if spec.startswith(":") and spec.endswith(":"):
                alignment.append("center")
            elif spec.endswith(":"):
                alignment.append("right")
            else:
                alignment.append("left")
        body_start = 2
    else:
        alignment = ["left"] * len(header)

    def row_html(values, tag):
        out = []
        for index, value in enumerate(values):
            align = alignment[index] if index < len(alignment) else "left"
            out.append(f'<{tag} style="text-align:{align}">'
                       f"{inline(value, images, source)}</{tag}>")
        return "<tr>" + "".join(out) + "</tr>"

    head = row_html(header, "th") if any(header) else ""
    body_rows = [cells(r) for r in rows[body_start:]]
    body = "".join(row_html(r, "td") for r in body_rows)

    # Tabelas cuja primeira coluna é SÓ uma tecla ou um nome em monoespaço
    # ganham uma classe, e a folha de estilo encolhe essa coluna ao conteúdo.
    # Detectado em vez de marcado à mão: quem escreve um capítulo não deve ter de
    # se lembrar de uma classe de CSS.
    compact = bool(body_rows) and all(
        re.fullmatch(r"`[^`]+`", (r[0] if r else "").strip()) for r in body_rows)
    css_class = ' class="keys"' if compact else ""
    return f"<table{css_class}><thead>{head}</thead><tbody>{body}</tbody></table>"


# --- montagem ------------------------------------------------------------

def revision() -> str:
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "describe", "--always", "--dirty"],
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() or "sem git"
    except Exception:
        return "sem git"


def find_chrome() -> str:
    for candidate in CHROME_CANDIDATES:
        if pathlib.Path(candidate).exists():
            return candidate
    raise BuildError("nenhum Chrome/Chromium encontrado; o PDF sai dele "
                     "(HTML em build/manual/manual.html continua a ser gerado)")


def main(argv) -> int:
    chapters = sorted(p for p in MANUAL.glob("*.md") if not p.name.startswith("_")
                      and p.name != "README.md")
    # `--only 04` compila um capítulo sozinho, para quem está a escrevê-lo: uma
    # volta de revisão passa de trinta segundos para dois.
    if "--only" in argv:
        wanted = argv[argv.index("--only") + 1]
        chapters = [c for c in chapters if c.stem.startswith(wanted)]
        if not chapters:
            raise BuildError(f"--only {wanted}: nenhum capítulo corresponde")
    if not chapters:
        print("SKIP: docs/manual não tem capítulos", file=sys.stderr)
        return 77

    keys = load_keys()
    images: list = []
    sections = []
    toc = []
    for chapter in chapters:
        text = expand(chapter.read_text(), chapter, keys, {chapter})
        title = next((l[2:].strip() for l in text.split("\n") if l.startswith("# ")),
                     chapter.stem)
        anchor = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
        if chapter.stem != "00-capa":
            toc.append((title, anchor))
        sections.append(f'<section class="chapter" id="c-{chapter.stem}">'
                        f'{render(text, chapter, images)}</section>')

    toc_html = "".join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for t, a in toc)
    css = (MANUAL / "manual.css").read_text()
    document = f"""<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<title>Spaceflight — Manual do piloto</title>
<style>{css}</style></head><body>
{sections[0] if sections else ""}
<section class="chapter toc"><h1 id="sumario">Sumário</h1><ol>{toc_html}</ol></section>
{"".join(sections[1:])}
<footer class="colofon">Compilado de <code>docs/manual/</code> por
<code>scripts/build_manual.py</code> — revisão {html.escape(revision())}.
As teclas vêm do Input Map; as figuras, de <code>scripts/m7_screenshots.sh</code>.</footer>
</body></html>"""

    BUILD.mkdir(parents=True, exist_ok=True)
    html_path = BUILD / "manual.html"
    html_path.write_text(document)
    print(f"{len(chapters)} capítulos, {len(set(images))} figuras -> "
          f"{html_path.relative_to(ROOT)}")

    output_pdf = (BUILD / "preview.pdf") if "--only" in argv else OUTPUT_PDF
    chrome = find_chrome()
    result = subprocess.run(
        [chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer",
         "--no-sandbox", f"--print-to-pdf={output_pdf}", html_path.as_uri()],
        capture_output=True, text=True, timeout=300)
    if not output_pdf.exists():
        raise BuildError(f"o Chrome não escreveu o PDF:\n{result.stderr[-2000:]}")

    blob = output_pdf.read_bytes()
    pages = blob.count(b"/Type /Page\n") or blob.count(b"/Type/Page")
    print(f"{output_pdf.relative_to(ROOT)}  "
          f"{output_pdf.stat().st_size / 1024 / 1024:.1f} MB  ~{pages} páginas")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except BuildError as error:
        print(f"ERRO: {error}", file=sys.stderr)
        sys.exit(1)
