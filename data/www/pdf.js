// Generador de PDF mínimo, sin librerías externas.
// Usa las fuentes estándar Helvetica / Helvetica-Bold con codificación WinAnsi
// (admite tildes, ñ, ¿, ¡ y °). Tamaño A4, coordenadas en puntos desde arriba a la izquierda.
"use strict";

const PDF_ANCHOS = (() => {
  // Anchos (1/1000 em) de los caracteres 32–126 en Helvetica y Helvetica-Bold
  const n = [278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,334,260,334,584];
  const b = [278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,389,280,389,584];
  return { n, b };
})();

// Letras con tilde -> letra base (para el ancho)
const PDF_BASE = { "á":"a","é":"e","í":"i","ó":"o","ú":"u","ü":"u","ñ":"n","Á":"A","É":"E","Í":"I","Ó":"O","Ú":"U","Ü":"U","Ñ":"N","ç":"c","Ç":"C","à":"a","è":"e","ò":"o" };
const PDF_WIN = { "€":0x80, "…":0x85, "•":0x95, "–":0x96, "—":0x97, "‘":0x91, "’":0x92, "“":0x93, "”":0x94 };

function pdfCodificar(s) {
  s = String(s).replace(/λ/g, "lambda").replace(/→/g, "->").replace(/−/g, "-");
  const out = [];
  for (const ch of s) {
    const c = ch.codePointAt(0);
    if (PDF_WIN[ch]) out.push(PDF_WIN[ch]);
    else if (c >= 32 && c <= 126) out.push(c);
    else if (c >= 0xA0 && c <= 0xFF) out.push(c);
    else out.push(63); // ?
  }
  return out;
}

class Pdf {
  constructor() {
    this.W = 595.28; this.H = 841.89;
    this.paginas = [];
    this.nuevaPagina();
  }
  nuevaPagina() { this.ops = []; this.paginas.push(this.ops); }

  ancho(texto, tam, negrita) {
    const t = negrita ? PDF_ANCHOS.b : PDF_ANCHOS.n;
    let w = 0;
    for (const ch of String(texto)) {
      const base = PDF_BASE[ch] || ch;
      const c = base.codePointAt(0);
      if (c >= 32 && c <= 126) w += t[c - 32];
      else if (ch === "°") w += 400;
      else if (ch === "¿") w += 611;
      else if (ch === "¡") w += 333;
      else if (ch === "–") w += 556;
      else if (ch === "—" || ch === "…") w += 1000;
      else w += 556;
    }
    return w * tam / 1000;
  }

  color(hex) {
    const v = parseInt(hex.slice(1), 16);
    return [((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255].map(x => x.toFixed(3)).join(" ");
  }

  texto(x, y, s, o = {}) {
    const tam = o.tam || 10, neg = !!o.negrita;
    let px = x;
    if (o.alinear === "derecha") px = x - this.ancho(s, tam, neg);
    if (o.alinear === "centro") px = x - this.ancho(s, tam, neg) / 2;
    const bytes = pdfCodificar(s);
    let str = "";
    for (const b of bytes) {
      if (b === 40 || b === 41 || b === 92) str += "\\" + String.fromCharCode(b);
      else if (b < 32 || b > 126) str += "\\" + b.toString(8).padStart(3, "0");
      else str += String.fromCharCode(b);
    }
    this.ops.push(`BT ${this.color(o.color || "#111111")} rg /${neg ? "F2" : "F1"} ${tam} Tf ${px.toFixed(2)} ${(this.H - y).toFixed(2)} Td (${str}) Tj ET`);
  }

  // Devuelve las líneas de un texto partido para un ancho máximo
  partir(s, anchoMax, tam, neg) {
    const palabras = String(s).split(/\s+/);
    const lineas = [];
    let linea = "";
    for (const p of palabras) {
      const prueba = linea ? linea + " " + p : p;
      if (this.ancho(prueba, tam, neg) > anchoMax && linea) { lineas.push(linea); linea = p; }
      else linea = prueba;
    }
    if (linea) lineas.push(linea);
    return lineas;
  }

  rect(x, y, w, h, relleno) {
    this.ops.push(`${this.color(relleno)} rg ${x.toFixed(2)} ${(this.H - y - h).toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)} re f`);
  }

  linea(x1, y1, x2, y2, color = "#CCCCCC", grosor = 0.6) {
    this.ops.push(`${this.color(color)} RG ${grosor} w ${x1.toFixed(2)} ${(this.H - y1).toFixed(2)} m ${x2.toFixed(2)} ${(this.H - y2).toFixed(2)} l S`);
  }

  generar() {
    const enc = new TextEncoder();
    const partes = [];
    const offsets = [];
    let pos = 0;
    const add = (s) => { const b = typeof s === "string" ? enc.encode(s) : s; partes.push(b); pos += b.length; };
    const obj = (n, cuerpo) => { offsets[n] = pos; add(`${n} 0 obj\n${cuerpo}\nendobj\n`); };

    add("%PDF-1.4\n");
    const nPag = this.paginas.length;
    // 1 catálogo, 2 páginas, 3 F1, 4 F2, luego (página, contenido) por cada página
    const kids = [];
    for (let i = 0; i < nPag; i++) kids.push(`${5 + i * 2} 0 R`);
    obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(2, `<< /Type /Pages /Kids [${kids.join(" ")}] /Count ${nPag} >>`);
    obj(3, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    obj(4, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    for (let i = 0; i < nPag; i++) {
      const contenido = this.paginas[i].join("\n");
      const pn = 5 + i * 2, cn = pn + 1;
      obj(pn, `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${this.W} ${this.H}] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents ${cn} 0 R >>`);
      obj(cn, `<< /Length ${contenido.length} >>\nstream\n${contenido}\nendstream`);
    }
    const nObj = 5 + nPag * 2;
    const xref = pos;
    let x = `xref\n0 ${nObj}\n0000000000 65535 f \n`;
    for (let i = 1; i < nObj; i++) x += String(offsets[i]).padStart(10, "0") + " 00000 n \n";
    add(x);
    add(`trailer\n<< /Size ${nObj} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`);
    const total = new Uint8Array(pos);
    let o = 0;
    for (const p of partes) { total.set(p, o); o += p.length; }
    return total;
  }
}
