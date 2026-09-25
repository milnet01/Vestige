from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

def text_paths(font, text, x, baseline, size, tracking):
    gs = font.getGlyphSet(); cmap = font.getBestCmap(); upm = font['head'].unitsPerEm
    s = size / upm; out = []
    for ch in text:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (s, 0, 0, -s, x, baseline)))
        out.append(pen.getCommands())
        x += gs[g].width * s + tracking
    return ' '.join(out), x - tracking

big = instantiateVariableFont(TTFont('Cinzel.ttf'), {'wght': 700})
small = instantiateVariableFont(TTFont('Cinzel.ttf'), {'wght': 600})

name_d, name_end = text_paths(big, 'VESTIGE', 296, 196, 128, 10)
eng_d, eng_end = text_paths(small, 'ENGINE', 0, 0, 44, 30)
# centre "ENGINE" under "VESTIGE"
name_w = name_end - 296
eng_x = 296 + (name_w - eng_end) / 2
eng_d, _ = text_paths(small, 'ENGINE', eng_x, 262, 44, 30)
W = int(name_end + 30)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} 320" width="{W}" height="320">
  <title>Vestige Engine</title>
  <defs>
    <linearGradient id="glow" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffd978"/>
      <stop offset="1" stop-color="#e89a2c"/>
    </linearGradient>
    <linearGradient id="beam" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#fff3c4" stop-opacity="0.95"/>
      <stop offset="1" stop-color="#ffd978" stop-opacity="0.55"/>
    </linearGradient>
  </defs>
  <!-- Doorway mark: stone arch with light pouring through -->
  <path d="M40 290 V130 A95 95 0 0 1 230 130 V290 Z" fill="none" stroke="#ede3cf" stroke-width="16" stroke-linejoin="round"/>
  <path d="M68 282 V134 A67 67 0 0 1 202 134 V282 Z" fill="url(#glow)"/>
  <path d="M120 72 L150 72 L178 282 L92 282 Z" fill="url(#beam)"/>
  <path d="M20 298 H250" stroke="#ede3cf" stroke-width="12" stroke-linecap="round"/>
  <path d="M123 38 H147 L142 60 H128 Z" fill="#ede3cf"/>
  <!-- Wordmark (Cinzel, SIL OFL 1.1, converted to outlines) -->
  <path d="{name_d}" fill="#ede3cf"/>
  <path d="{eng_d}" fill="#f0b64a"/>
</svg>
'''
open('vestige-logo.svg', 'w').write(svg)
print(W, 'x 320 ratio', round(W/320, 2))
