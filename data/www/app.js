// Web del aparato de diagnosis NMAX 125 (se abre en Safari del iPhone)
"use strict";

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

// PIDs
const P = { CARGA: 4, TEMP: 5, MAP: 11, RPM: 12, VEL: 13, AVANCE: 14, AIRE: 15, ACEL: 17, LAMBDA: 20, BAT: 66 };

const S = {
  ws: null, wsAbierto: false, vivo: null, pids: [], pidsCargados: false, averias: null, dv: -1,
  estado: null, pila: ["inicio"], mant: null, msMant: 0, batLimite: 11.8, viaje: null, serie: "rpm",
  conexionPrevia: -1, simPrevio: -1,
};

// ------------------------------------------------------------------ formato español

function num(v, dec = 0) {
  if (v === null || v === undefined || !isFinite(v)) return "—";
  const neg = v < 0;
  let [ent, frac] = Math.abs(v).toFixed(dec).split(".");
  if (ent.length > 3) ent = ent.replace(/\B(?=(\d{3})+(?!\d))/g, ".");
  return (neg && Number(v.toFixed(dec)) !== 0 ? "−" : "") + ent + (frac ? "," + frac : "");
}
const dos = (n) => String(n).padStart(2, "0");
function fecha(epoch, conHora = true) {
  if (!epoch) return "—";
  const d = new Date(epoch * 1000);
  const f = `${dos(d.getDate())}/${dos(d.getMonth() + 1)}/${d.getFullYear()}`;
  return conHora ? `${f} ${dos(d.getHours())}:${dos(d.getMinutes())}` : f;
}
function duracion(seg) {
  seg = Math.round(seg || 0);
  const h = Math.floor(seg / 3600), m = Math.floor((seg % 3600) / 60), s = seg % 60;
  if (h) return `${h} h ${dos(m)} min`;
  if (m) return `${m} min ${dos(s)} s`;
  return `${s} s`;
}
function tamano(b) {
  if (b >= 1048576) return num(b / 1048576, 1) + " MB";
  return num(b / 1024, 0) + " KB";
}
function leerNumero(s) {
  const v = parseFloat(String(s).replace(/\./g, "").replace(",", "."));
  return isFinite(v) ? v : NaN;
}

// ------------------------------------------------------------------ red

async function api(ruta, datos) {
  const op = datos === undefined ? {} : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(datos),
  };
  const r = await fetch(ruta, op);
  let j = {};
  try { j = await r.json(); } catch (e) { /* sin cuerpo */ }
  if (!r.ok || j.ok === false) throw new Error(j.msg || "Error " + r.status);
  return j;
}

function enviarHora() {
  api("/api/hora", { epoch: Math.floor(Date.now() / 1000) }).catch(() => {});
}

function conectarWs() {
  if (S.ws && (S.ws.readyState === 0 || S.ws.readyState === 1)) return;
  const ws = new WebSocket(`ws://${location.host || "192.168.4.1"}/ws`);
  S.ws = ws;
  clearTimeout(S.tReconexion);
  ws.onopen = () => {
    S.wsAbierto = true;
    S.dv = -1;               // fuerza a releer averías y PIDs (la ESP32 puede haberse reiniciado)
    S.pidsCargados = false;
    enviarHora();
    cargarInicial();
    pintarEstado();
  };
  ws.onclose = () => { S.wsAbierto = false; pintarEstado(); S.tReconexion = setTimeout(conectarWs, 1500); };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    if (m.t === "v") recibirVivo(m);
    else if (m.t === "e") recibirEvento(m);
  };
}

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") { conectarWs(); enviarHora(); }
});

async function cargarInicial() {
  try {
    S.estado = await api("/api/estado");
    S.batLimite = S.estado.bateriaBaja || 11.8;
  } catch (e) { /* se reintenta */ }
  cargarMant();
}

// ------------------------------------------------------------------ navegación

const VISTAS = {
  inicio: { t: "Inicio" }, datos: { t: "Datos en vivo" }, averias: { t: "Averías" }, mas: { t: "Más" },
  foto: { t: "Foto del fallo" }, historial: { t: "Historial de averías" }, grab: { t: "Grabación" },
  viaje: { t: "Trayecto" }, records: { t: "Récords" }, mant: { t: "Mantenimiento" }, ecu: { t: "Centralita" },
  pdf: { t: "Informe PDF" }, ota: { t: "Actualizar" }, ajustes: { t: "Ajustes" },
};
const PESTANAS = ["inicio", "datos", "averias", "mas"];
const alEntrar = {};

function mostrar(v) {
  $$(".vista").forEach((s) => s.classList.toggle("activa", s.id === "v-" + v));
  $("#titulo").textContent = VISTAS[v].t;
  $("#btn-atras").classList.toggle("oculto", S.pila.length <= 1);
  const raiz = S.pila[0];
  $$("#pestanas button").forEach((b) => b.classList.toggle("activa", b.dataset.tab === raiz));
  $("#contenido").scrollTop = 0;
  if (alEntrar[v]) alEntrar[v]();
}
function ir(v, arg) {
  S.arg = arg;
  if (PESTANAS.includes(v)) S.pila = [v];
  else S.pila.push(v);
  mostrar(v);
}
function atras() {
  if (S.pila.length > 1) S.pila.pop();
  mostrar(S.pila[S.pila.length - 1]);
}
const vistaActual = () => S.pila[S.pila.length - 1];

$$("#pestanas button").forEach((b) => b.addEventListener("click", () => ir(b.dataset.tab)));
$("#btn-atras").addEventListener("click", atras);
$$("[data-ir]").forEach((b) => b.addEventListener("click", () => ir(b.dataset.ir)));
$("#btn-ir-records").addEventListener("click", () => ir("records"));
$("#aviso-averias").addEventListener("click", () => ir("averias"));
$("#aviso-mant").addEventListener("click", () => ir("mant"));

// ------------------------------------------------------------------ avisos, hoja y alertas

let tAviso;
function aviso(msg, tipo = "") {
  const a = $("#aviso");
  a.textContent = msg;
  a.className = tipo;
  clearTimeout(tAviso);
  tAviso = setTimeout(() => a.classList.add("oculto"), 3800);
}

function confirmar({ titulo, html, si = "Aceptar", peligro = true, entrada = null }) {
  return new Promise((resolver) => {
    $("#hoja-titulo").textContent = titulo;
    $("#hoja-texto").innerHTML = html + (entrada
      ? `<input id="hoja-entrada" inputmode="decimal" autocomplete="off" value="${esc(entrada.valor ?? "")}" placeholder="${esc(entrada.placeholder ?? "")}">`
      : "");
    const bSi = $("#hoja-si");
    bSi.textContent = si;
    bSi.className = "boton " + (peligro ? "boton-peligro" : "boton-acento");
    $("#velo").classList.remove("oculto");
    $("#hoja").classList.remove("oculto");
    const cerrar = (res) => {
      $("#velo").classList.add("oculto");
      $("#hoja").classList.add("oculto");
      bSi.onclick = $("#hoja-no").onclick = $("#velo").onclick = null;
      resolver(res);
    };
    bSi.onclick = () => cerrar(entrada ? $("#hoja-entrada").value : true);
    $("#hoja-no").onclick = $("#velo").onclick = () => cerrar(entrada ? null : false);
    if (entrada) setTimeout(() => $("#hoja-entrada").focus(), 250);
  });
}

const AL = { temp: { armada: true }, bat: { armada: true, desde: 0 }, visible: null, cola: [] };
function mostrarAlerta(tipo, titulo, valor, texto, nivel = "peligro") {
  // Si ya hay una alerta en pantalla, la nueva espera a que se pulse «Entendido»
  if (AL.visible && AL.visible !== tipo) { AL.cola.push([tipo, titulo, valor, texto, nivel]); return; }
  AL.visible = tipo;
  $("#alerta").classList.remove("oculto");
  $("#alerta").className = nivel === "aviso" ? "aviso" : "";
  $("#alerta-titulo").textContent = titulo;
  $("#alerta-valor").textContent = valor;
  $("#alerta-texto").textContent = texto;
  if (navigator.vibrate) navigator.vibrate([200, 100, 200]);
}
$("#alerta-ok").addEventListener("click", () => {
  $("#alerta").classList.add("oculto");
  AL.visible = null;
  const siguiente = AL.cola.shift();
  if (siguiente) mostrarAlerta(...siguiente);
});

function revisarAlertas(m) {
  if (m.c !== 2) return;
  const t = m.v[P.TEMP], lim = m.tl;
  if (t !== undefined) {
    if (t >= lim && AL.temp.armada) {
      AL.temp.armada = false;
      mostrarAlerta("temp", "Motor muy caliente", `${num(t)} °C`, `Límite ${num(lim)} °C. Para y deja enfriar el motor.`);
    } else if (t < lim - 3) AL.temp.armada = true;
    if (AL.visible === "temp" && t >= lim) $("#alerta-valor").textContent = `${num(t)} °C`;
  }
  const b = m.v[P.BAT];
  if (b !== undefined && b < S.batLimite) {
    if (!AL.bat.desde) AL.bat.desde = Date.now();
    if (Date.now() - AL.bat.desde > 5000 && AL.bat.armada) {
      AL.bat.armada = false;
      mostrarAlerta("bat", "Batería baja", `${num(b, 1)} V`, `Por debajo de ${num(S.batLimite, 1)} V. Revisa la batería y la carga.`, "aviso");
    }
  } else {
    AL.bat.desde = 0;
    if (b !== undefined && b > S.batLimite + 0.3) AL.bat.armada = true;
  }
  if (AL.visible === "bat" && b !== undefined && b < S.batLimite) $("#alerta-valor").textContent = `${num(b, 1)} V`;
}

// ------------------------------------------------------------------ datos en vivo

function pintarEstado() {
  const e = $("#estado");
  const m = S.vivo;
  let txt, cls;
  if (!S.wsAbierto) { txt = "Sin WiFi"; cls = "peligro"; }
  else if (!m) { txt = "…"; cls = ""; }
  else if (m.c === 2) { txt = m.s ? "Conectado · SIM" : "Conectado"; cls = m.s ? "sim" : "ok"; }
  else if (m.c === 1) { txt = "Conectando…"; cls = "aviso"; }
  else { txt = "Sin centralita"; cls = "peligro"; }
  e.textContent = txt;
  e.className = "pastilla " + cls;
  const se = $("#splash-estado");
  if (se) se.textContent = m && m.c === 2 ? "Conectado" : "Conectando con la centralita…";
}

// Escritura en el DOM solo si el contenido cambia (llegan 5 mensajes por segundo)
function ponTexto(el, t) { if (el && el._t !== t) { el._t = t; el.textContent = t; } }
function ponHtml(el, h) { if (el && el._h !== h) { el._h = h; el.innerHTML = h; } }
const conUnidad = (valor, unidad) => (valor === "—" ? "—" : `${valor}<span class="uni secundario"> ${unidad}</span>`);

const D = {};   // elementos de Inicio que se actualizan en vivo
for (const [k, sel] of Object.entries({
  rpm: "#rpm-valor", barra: "#rpm-relleno", vel: "#vel-valor", insignia: "#insignia-averias",
  bandaAv: "#aviso-averias", memoria: "#aviso-memoria",
})) D[k] = $(sel);

function tarjeta(id, valor, unidad, nivel = "") {
  const t = D[id] || (D[id] = $(id));
  ponHtml(t.querySelector(".cifra"), conUnidad(valor, unidad));
  t.classList.toggle("aviso", nivel === "aviso");
  t.classList.toggle("peligro", nivel === "peligro");
}

function recibirVivo(m) {
  const previo = S.vivo;
  S.vivo = m;
  const v = m.v || {};
  // Inicio
  const rpm = v[P.RPM];
  ponTexto(D.rpm, num(rpm));
  const ancho = rpm === undefined ? "0" : Math.min(100, rpm / 100).toFixed(1) + "%";
  if (D.barra.style.width !== ancho) D.barra.style.width = ancho;
  ponHtml(D.vel, conUnidad(num(v[P.VEL]), "km/h"));
  const t = v[P.TEMP];
  tarjeta("#t-temp", num(t), "°C", t === undefined ? "" : t >= m.tl ? "peligro" : t >= m.tl - 8 ? "aviso" : "");
  const b = v[P.BAT];
  tarjeta("#t-bat", num(b, 1), "V", b !== undefined && b < S.batLimite ? "aviso" : "");
  tarjeta("#t-acel", num(v[P.ACEL]), "%");
  tarjeta("#t-aire", num(v[P.AIRE]), "°C");

  const nav = m.n + m.np;
  ponTexto(D.insignia, String(nav));
  D.insignia.classList.toggle("oculto", !nav);
  D.bandaAv.classList.toggle("oculto", !nav);
  if (nav) {
    ponTexto(D.bandaAv.querySelector("span"),
      `${m.n} ${m.n === 1 ? "avería guardada" : "averías guardadas"}` +
      (m.np ? ` · ${m.np} pendiente${m.np > 1 ? "s" : ""}` : "") + (m.mk && m.m ? " · testigo encendido" : ""));
  }
  D.memoria.classList.toggle("oculto", !m.fa);
  pintarGrabacion(m);
  pintarEstado();

  // Datos en vivo
  if (m.c === 2 && (!S.pidsCargados || (previo && previo.c !== 2) || S.simPrevio !== m.s)) cargarPids();
  S.simPrevio = m.s;
  if (vistaActual() === "datos") pintarDatos();
  if (m.dv !== S.dv) { S.dv = m.dv; cargarAverias(); }
  if (vistaActual() === "records") pintarMedir(m.z);
  revisarAlertas(m);
}

function pintarGrabacion(m) {
  for (const id of ["#btn-grabar", "#btn-grab2"]) {
    const bt = $(id);
    bt.classList.toggle("grabando", !!m.g);
    const icono = bt.querySelector("use");
    const href = m.g ? "#i-parar" : "#i-grabar";
    if (icono.getAttribute("href") !== href) icono.setAttribute("href", href);
    ponTexto(bt.querySelector("span"), m.g ? `Parar ${duracion(m.gs)}` : "Grabar");
  }
  if (vistaActual() === "grab") ponTexto($("#grab-titulo"), m.g ? `Grabando · ${duracion(m.gs)}` : "Sin grabar");
}

async function alternarGrabacion() {
  const g = S.vivo && S.vivo.g;
  try {
    await api(g ? "/api/viajes/parar" : "/api/viajes/iniciar", {});
    aviso(g ? "Trayecto guardado" : "Grabando trayecto", "ok");
    if (vistaActual() === "grab") alEntrar.grab();
  } catch (e) { aviso(e.message, "error"); }
}
$("#btn-grabar").addEventListener("click", alternarGrabacion);
$("#btn-grab2").addEventListener("click", alternarGrabacion);

async function cargarPids() {
  S.pidsCargados = true;
  try {
    const j = await api("/api/pids");
    S.pids = j.pids || [];
    construirDatos();
  } catch (e) { S.pidsCargados = false; }
}

function construirDatos() {
  const cont = $("#lista-datos");
  cont.innerHTML = S.pids.map((p) =>
    `<div class="valor" data-pid="${p.id}"><span class="nom">${esc(p.nombre)}</span><span><b class="num">—</b><span class="uni">${esc(p.unidad)}</span></span></div>`
  ).join("");
  S.celdas = S.pids.map((p) => [p, cont.querySelector(`[data-pid="${p.id}"] .num`)]);
  pintarDatos();
}

function pintarDatos() {
  const m = S.vivo;
  const sin = !m || m.c !== 2 || !S.pids.length;
  $("#datos-vacio").classList.toggle("oculto", !sin);
  if (!m) return;
  for (const [p, el] of S.celdas || []) ponTexto(el, num(m.v[p.id], p.dec));
}
alEntrar.datos = pintarDatos;

// ------------------------------------------------------------------ eventos del aparato

function recibirEvento(m) {
  if (m.e === "borrado") {
    aviso(m.msg, m.ok ? "ok" : "error");
    const b = $("#btn-borrar");
    b.disabled = false;
    cargarAverias();
  } else if (m.e === "050") {
    if (m.ok) aviso(`0–50 km/h en ${num(m.seg, 2)} s${m.record ? " · ¡nuevo récord!" : ""}`, "ok");
    else aviso(m.msg, "error");
    if (vistaActual() === "records") alEntrar.records();
  } else if (m.e === "grab") {
    aviso(m.msg, m.ok ? "" : "error");
    if (vistaActual() === "grab") alEntrar.grab();
  }
}

// ------------------------------------------------------------------ averías

async function cargarAverias() {
  try {
    S.averias = await api("/api/averias");
  } catch (e) { return; }
  pintarAverias();
  if (vistaActual() === "foto") alEntrar.foto();
}

function filaDtc(cod, pendiente, foto) {
  const tieneFoto = foto && foto.valida && foto.dtc === cod;
  return `<button class="fila dtc ${pendiente ? "pend" : ""}" data-dtc="${cod}">
    <span class="cod">${cod}</span>
    <span class="txt"><span class="desc">${esc(descripcionDtc(cod))}</span>
    <span class="sub">${pendiente ? "Pendiente de confirmar" : "Guardada"}${tieneFoto ? " · con foto del fallo" : ""}</span></span>
    <svg class="chev"><use href="#i-flecha"/></svg></button>`;
}

function pintarAverias() {
  const a = S.averias;
  if (!a) return;
  const mil = $("#mil");
  mil.classList.toggle("encendido", a.milConocido && a.mil);
  mil.classList.toggle("apagado", a.milConocido && !a.mil);
  $("#mil-texto").textContent = !a.milConocido ? "Testigo del motor: desconocido" : a.mil ? "Testigo del motor encendido" : "Testigo del motor apagado";
  $("#mil-sub").textContent = a.conexion !== 2 ? "Sin conexión con la centralita" : a.leidas ? `Leído hace ${duracion(a.hace)}` : "Leyendo…";
  $("#lista-guardadas").innerHTML = a.guardadas.length ? a.guardadas.map((c) => filaDtc(c, false, a.foto)).join("") : `<p class="vacio">Sin averías guardadas</p>`;
  $("#lista-pendientes").innerHTML = a.pendientes.length ? a.pendientes.map((c) => filaDtc(c, true, a.foto)).join("") : `<p class="vacio">Sin averías pendientes</p>`;
  $$("[data-dtc]").forEach((b) => b.addEventListener("click", () => ir("foto", b.dataset.dtc)));
}
alEntrar.averias = cargarAverias;

$("#btn-releer").addEventListener("click", async () => {
  try { await api("/api/averias/leer", {}); aviso("Leyendo averías…"); setTimeout(cargarAverias, 1500); }
  catch (e) { aviso(e.message, "error"); }
});

$("#btn-borrar").addEventListener("click", async () => {
  const a = S.averias;
  const n = a ? a.guardadas.length + a.pendientes.length : 0;
  const rpm = S.vivo && S.vivo.v[P.RPM];
  const ok = await confirmar({
    titulo: "Borrar averías",
    html: `<p>Se borrarán ${n ? `<b>${n} ${n === 1 ? "código" : "códigos"}</b>` : "los códigos"} y la foto del fallo de la centralita, y se apagará el testigo.</p>
      <p>Solo se puede hacer con el <b>motor parado</b> y el contacto puesto.${rpm > 0 ? ` <b style="color:var(--peligro)">Ahora el motor está en marcha (${num(rpm)} rpm).</b>` : ""}</p>
      <p>Quedarán apuntados en el historial.</p>`,
    si: "Borrar averías",
  });
  if (!ok) return;
  try {
    $("#btn-borrar").disabled = true;
    await api("/api/averias/borrar", { confirmar: true });
    aviso("Borrando…");
    setTimeout(() => { $("#btn-borrar").disabled = false; }, 8000);
  } catch (e) {
    $("#btn-borrar").disabled = false;
    aviso(e.message, "error");
  }
});

$("#btn-historial").addEventListener("click", () => ir("historial"));

alEntrar.foto = () => {
  const cod = S.arg;
  const a = S.averias;
  $("#foto-cod").textContent = cod;
  $("#foto-desc").textContent = descripcionDtc(cod);
  const f = a && a.foto;
  const lista = $("#foto-lista");
  if (f && f.valida && f.dtc === cod) {
    $("#foto-nota").textContent = "Valores que guardó la centralita en el momento del fallo:";
    lista.innerHTML = (f.valores || []).map((p) =>
      `<div class="valor"><span class="nom">${esc(p.nombre)}</span><span><b class="num">${num(p.v, p.dec)}</b><span class="uni">${esc(p.unidad)}</span></span></div>`).join("");
  } else {
    lista.innerHTML = "";
    $("#foto-nota").textContent = f && f.valida
      ? `La centralita solo guarda una foto del fallo, y es la de la avería ${f.dtc}.`
      : "La centralita no tiene foto del fallo guardada para esta avería.";
  }
};

alEntrar.historial = async () => {
  const cont = $("#lista-historial");
  cont.innerHTML = `<p class="vacio">Cargando…</p>`;
  try {
    const j = await api("/api/historial");
    const h = (j.h || []).slice().sort((x, y) => (y.d || 0) - (x.d || 0));
    cont.innerHTML = h.length ? h.map((e) => `
      <div class="fila dtc ${e.t === "p" ? "pend" : ""}">
        <span class="cod">${esc(e.c)}</span>
        <span class="txt"><span class="desc">${esc(descripcionDtc(e.c))}</span>
          <span class="sub">Detectada ${fecha(e.d)} · ${e.t === "p" ? "pendiente" : "guardada"}</span>
          ${e.e === "b" ? `<span class="sub">Borrada ${fecha(e.h)} · ${esc(e.m || "")}</span>` : ""}</span>
        <span class="etiqueta ${e.e === "a" ? "peligro" : "ok"}">${e.e === "a" ? "Activa" : "Borrada"}</span>
      </div>`).join("") : `<p class="vacio">El historial está vacío.</p>`;
  } catch (e) { cont.innerHTML = `<p class="vacio">${esc(e.message)}</p>`; }
};

// ------------------------------------------------------------------ grabación y trayectos

alEntrar.grab = async () => {
  if (S.vivo) pintarGrabacion(S.vivo);
  try {
    const j = await api("/api/viajes");
    const usado = j.total - j.libre;
    $("#mem-texto").textContent = `${tamano(j.libre)} libres de ${tamano(j.total)}`;
    const barra = $("#mem-barra");
    barra.style.width = (100 * usado / j.total).toFixed(1) + "%";
    barra.style.background = j.aviso ? "var(--aviso)" : "var(--acento)";
    const v = (j.viajes || []).sort((a, b) => b.inicio - a.inicio);
    $("#viajes-vacio").classList.toggle("oculto", v.length > 0);
    $("#lista-viajes").innerHTML = v.map((x) => `
      <button class="fila" data-viaje="${esc(x.nombre)}">
        <svg><use href="#i-grabar"/></svg>
        <span class="txt"><b>${fecha(x.inicio)}</b><span class="sub secundario">${duracion(x.seg)} · ${tamano(x.bytes)}${x.grabando ? " · grabando" : ""}</span></span>
        <svg class="chev"><use href="#i-flecha"/></svg></button>`).join("");
    $$("[data-viaje]").forEach((b) => b.addEventListener("click", () => ir("viaje", b.dataset.viaje)));
  } catch (e) { aviso(e.message, "error"); }
};

const SERIES = [
  { col: "rpm", nom: "Revoluciones", uni: "rpm", dec: 0 },
  { col: "velocidad_kmh", nom: "Velocidad", uni: "km/h", dec: 0 },
  { col: "temp_motor_c", nom: "Temp. motor", uni: "°C", dec: 0 },
  { col: "acelerador_pct", nom: "Acelerador", uni: "%", dec: 0 },
  { col: "bateria_v", nom: "Batería", uni: "V", dec: 1 },
  { col: "temp_aire_c", nom: "Aire admisión", uni: "°C", dec: 0 },
  { col: "presion_adm_kpa", nom: "Presión adm.", uni: "kPa", dec: 0 },
  { col: "carga_pct", nom: "Carga", uni: "%", dec: 0 },
  { col: "avance_grados", nom: "Avance", uni: "°", dec: 1 },
  { col: "lambda_v", nom: "Lambda", uni: "V", dec: 3 },
];

function parsearCsv(texto) {
  const lineas = texto.trim().split(/\r?\n/);
  const cab = lineas.shift().split(",");
  const cols = {};
  cab.forEach((c) => (cols[c] = []));
  for (const l of lineas) {
    const p = l.split(",");
    cab.forEach((c, i) => cols[c].push(p[i] === "" || p[i] === undefined ? null : parseFloat(p[i])));
  }
  return cols;
}

alEntrar.viaje = async () => {
  const nombre = S.arg;
  $("#titulo").textContent = "Trayecto " + fecha(parseInt(nombre, 10));
  $("#btn-csv").href = `/viaje?n=${encodeURIComponent(nombre)}&descargar=1`;
  $("#btn-csv").setAttribute("download", `nmax-${nombre}`);
  try {
    const r = await fetch(`/viaje?n=${encodeURIComponent(nombre)}`);
    if (!r.ok) throw new Error("No se puede leer el trayecto");
    S.viaje = parsearCsv(await r.text());
  } catch (e) { aviso(e.message, "error"); return; }
  const disp = SERIES.filter((s) => (S.viaje[s.col] || []).some((x) => x !== null));
  if (!disp.find((s) => s.col === S.serie)) S.serie = disp.length ? disp[0].col : null;
  $("#viaje-series").innerHTML = disp.map((s) => `<button class="chip ${s.col === S.serie ? "activo" : ""}" data-serie="${s.col}">${s.nom}</button>`).join("");
  $$("[data-serie]").forEach((b) => b.addEventListener("click", () => {
    S.serie = b.dataset.serie;
    $$("[data-serie]").forEach((x) => x.classList.toggle("activo", x === b));
    dibujarGrafica();
  }));
  const maxDe = (c) => { let m = -Infinity; for (const x of S.viaje[c] || []) if (x !== null && x > m) m = x; return m; };
  const t = S.viaje.t_s || [];
  const res = [
    ["Duración", duracion(t[t.length - 1] || 0), ""],
    ["Vel. máxima", num(isFinite(maxDe("velocidad_kmh")) ? maxDe("velocidad_kmh") : NaN), "km/h"],
    ["Rpm máximas", num(isFinite(maxDe("rpm")) ? maxDe("rpm") : NaN), "rpm"],
    ["Temp. máxima", num(isFinite(maxDe("temp_motor_c")) ? maxDe("temp_motor_c") : NaN), "°C"],
  ];
  $("#viaje-resumen").innerHTML = res.map(([e, v, u]) =>
    `<div class="tarjeta mini"><span class="etq">${e}</span><b class="cifra">${v}${u && v !== "—" ? `<span class="uni secundario"> ${u}</span>` : ""}</b></div>`).join("");
  $("#grafica-cursor").textContent = "Toca la gráfica para ver los valores.";
  requestAnimationFrame(dibujarGrafica);
};

function dibujarGrafica(cursorX) {
  const cv = $("#grafica");
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  if (!W) return;
  cv.width = W * dpr; cv.height = H * dpr;
  const g = cv.getContext("2d");
  g.scale(dpr, dpr);
  g.clearRect(0, 0, W, H);
  const s = SERIES.find((x) => x.col === S.serie);
  if (!s || !S.viaje) return;
  const xs = S.viaje.t_s || [], ys = S.viaje[s.col] || [];
  let min = Infinity, max = -Infinity;
  for (const v of ys) if (v !== null) { if (v < min) min = v; if (v > max) max = v; }
  if (min === Infinity) return;
  if (s.col === "rpm" || s.col === "velocidad_kmh" || s.col.endsWith("_pct")) min = Math.min(0, min);
  if (max - min < 1e-6) { max += 1; min -= 1; }
  const pad = { l: 50, r: 10, t: 10, b: 24 };
  const tMax = xs[xs.length - 1] || 1;
  const X = (t) => pad.l + (t / tMax) * (W - pad.l - pad.r);
  const Y = (v) => pad.t + (1 - (v - min) / (max - min)) * (H - pad.t - pad.b);
  const css = getComputedStyle(document.documentElement);
  g.font = "500 12px 'Barlow Semi Condensed', sans-serif";
  g.fillStyle = css.getPropertyValue("--texto2");
  g.strokeStyle = css.getPropertyValue("--borde");
  g.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const v = min + (max - min) * i / 4, y = Y(v);
    g.beginPath(); g.moveTo(pad.l, y); g.lineTo(W - pad.r, y); g.stroke();
    g.textAlign = "right"; g.fillText(num(v, s.dec > 1 ? 2 : s.dec), pad.l - 6, y + 4);
  }
  g.textAlign = "center";
  // Eje de tiempo con etiquetas cortas para que no se monten
  const corta = (seg) => {
    seg = Math.round(seg);
    const h = Math.floor(seg / 3600), m = Math.floor((seg % 3600) / 60);
    return h ? `${h} h ${dos(m)}` : m ? `${m} min` : `${seg} s`;
  };
  for (let i = 0; i <= 3; i++) {
    const t = tMax * i / 3;
    g.textAlign = i === 0 ? "left" : i === 3 ? "right" : "center";
    g.fillText(corta(t), X(t), H - 6);
  }
  g.strokeStyle = css.getPropertyValue("--acento");
  g.lineWidth = 2;
  // Un trayecto largo tiene decenas de miles de puntos: se dibuja el mínimo y el máximo de cada píxel
  g.beginPath();
  let dentro = false, colPx = -1, lo = 0, hi = 0;
  const volcar = () => {
    if (colPx < 0) return;
    if (!dentro) { g.moveTo(colPx, Y(lo)); dentro = true; } else g.lineTo(colPx, Y(lo));
    if (hi !== lo) g.lineTo(colPx, Y(hi));
  };
  for (let i = 0; i < xs.length; i++) {
    if (ys[i] === null) { volcar(); colPx = -1; dentro = false; continue; }
    const px = Math.round(X(xs[i]));
    if (px !== colPx) { volcar(); colPx = px; lo = hi = ys[i]; }
    else { if (ys[i] < lo) lo = ys[i]; if (ys[i] > hi) hi = ys[i]; }
  }
  volcar();
  g.stroke();
  if (cursorX !== undefined) {
    const t = Math.max(0, Math.min(tMax, (cursorX - pad.l) / (W - pad.l - pad.r) * tMax));
    let a = 0, b = xs.length - 1;
    while (b - a > 1) { const c = (a + b) >> 1; if (xs[c] < t) a = c; else b = c; }
    const k = Math.abs(xs[a] - t) <= Math.abs(xs[b] - t) ? a : b;
    g.strokeStyle = css.getPropertyValue("--texto");
    g.lineWidth = 1;
    g.beginPath(); g.moveTo(X(xs[k]), pad.t); g.lineTo(X(xs[k]), H - pad.b); g.stroke();
    const partes = SERIES.filter((x) => S.viaje[x.col] && S.viaje[x.col][k] !== null && S.viaje[x.col][k] !== undefined)
      .slice(0, 4).map((x) => `${x.nom} ${num(S.viaje[x.col][k], x.dec)} ${x.uni}`);
    $("#grafica-cursor").textContent = `${duracion(xs[k])}: ` + partes.join(" · ");
  }
}
const cursorGrafica = (ev) => {
  const r = $("#grafica").getBoundingClientRect();
  const p = ev.touches ? ev.touches[0] : ev;
  dibujarGrafica(p.clientX - r.left);
};
$("#grafica").addEventListener("touchstart", cursorGrafica, { passive: true });
$("#grafica").addEventListener("touchmove", cursorGrafica, { passive: true });
$("#grafica").addEventListener("mousedown", cursorGrafica);
window.addEventListener("resize", () => { if (vistaActual() === "viaje") dibujarGrafica(); });

$("#btn-borrar-viaje").addEventListener("click", async () => {
  const nombre = S.arg;
  const ok = await confirmar({ titulo: "Borrar trayecto", html: `<p>Se borrará el trayecto del <b>${fecha(parseInt(nombre, 10))}</b>.</p>`, si: "Borrar" });
  if (!ok) return;
  try { await api("/api/viajes/borrar", { nombre }); aviso("Trayecto borrado", "ok"); atras(); }
  catch (e) { aviso(e.message, "error"); }
});

// ------------------------------------------------------------------ récords

alEntrar.records = async () => {
  try {
    const r = await api("/api/records");
    $("#rec-vmax").innerHTML = r.vmax ? `${num(r.vmax)} <span class="uni secundario">km/h</span>` : "—";
    $("#rec-fvmax").textContent = r.fVmax ? fecha(r.fVmax) : "";
    $("#rec-rpm").innerHTML = r.rpmMax ? `${num(r.rpmMax)} <span class="uni secundario">rpm</span>` : "—";
    $("#rec-frpm").textContent = r.fRpm ? fecha(r.fRpm) : "";
    $("#rec-050").innerHTML = r.t050 ? `${num(r.t050, 2)} <span class="uni secundario">s</span>` : "—";
    $("#rec-f050").textContent = r.f050 ? fecha(r.f050) : "";
    pintarMedir(r.medir);
  } catch (e) { aviso(e.message, "error"); }
};

function pintarMedir(z) {
  const t = {
    0: ["Medir 0–50", "Pulsa el botón con la moto parada y acelera a fondo hasta pasar de 50 km/h.", "Medir 0–50"],
    1: ["Para la moto", "La medición empieza con la moto parada (0 km/h).", "Cancelar"],
    2: ["¡Listo!", "Acelera a fondo cuando quieras. El tiempo empieza al moverte.", "Cancelar"],
    3: ["Midiendo…", "Sigue acelerando hasta pasar de 50 km/h.", "Cancelar"],
  }[z || 0];
  $("#medir-titulo").textContent = t[0];
  $("#medir-texto").textContent = t[1];
  const b = $("#btn-medir");
  b.querySelector("span").textContent = t[2];
  b.className = "boton " + (z ? "" : "boton-acento");
  b.dataset.z = z || 0;
}
$("#btn-medir").addEventListener("click", async () => {
  const z = +$("#btn-medir").dataset.z;
  try { await api("/api/records/050", { accion: z ? "cancelar" : "iniciar" }); pintarMedir(z ? 0 : 1); }
  catch (e) { aviso(e.message, "error"); }
});
$("#btn-reiniciar-rec").addEventListener("click", async () => {
  const ok = await confirmar({ titulo: "Reiniciar récords", html: "<p>Se pondrán a cero la velocidad máxima, las revoluciones máximas y el tiempo de 0 a 50 km/h.</p>", si: "Reiniciar" });
  if (!ok) return;
  try { await api("/api/records/reiniciar", {}); aviso("Récords reiniciados", "ok"); alEntrar.records(); }
  catch (e) { aviso(e.message, "error"); }
});

// ------------------------------------------------------------------ mantenimiento

function estadoItem(it, km) {
  const recorrido = km - it.km;
  const falta = it.int - recorrido;
  const nivel = falta <= 0 ? "peligro" : falta <= Math.max(300, it.int * 0.1) ? "aviso" : "";
  return { recorrido, falta, nivel, pct: Math.max(0, Math.min(100, 100 * recorrido / it.int)) };
}

async function cargarMant() {
  try { S.mant = await api("/api/mant"); S.msMant = Date.now(); } catch (e) { return; }
  pintarAvisoMant();
}

function pintarAvisoMant() {
  const m = S.mant;
  if (!m) return;
  const pend = m.items.map((it) => ({ it, e: estadoItem(it, m.km) })).filter((x) => x.e.nivel);
  const b = $("#aviso-mant");
  b.classList.toggle("oculto", !pend.length);
  $("#mas-mant").classList.toggle("oculto", !pend.length);
  if (pend.length) {
    b.className = "banda " + (pend.some((x) => x.e.nivel === "peligro") ? "banda-peligro" : "banda-aviso");
    b.querySelector("span").textContent = pend.map((x) => x.e.falta <= 0
      ? `${x.it.nombre}: toca cambiarlo` : `${x.it.nombre}: faltan ${num(x.e.falta)} km`).join(" · ");
  }
}
setInterval(() => { if (S.wsAbierto && Date.now() - S.msMant > 60000) cargarMant(); }, 15000);

alEntrar.mant = async () => {
  await cargarMant();
  const m = S.mant;
  if (!m) return;
  $("#mant-km").innerHTML = `${num(m.km)} <span class="uni secundario">km</span>`;
  $("#mant-km-origen").textContent = m.kmCentralita
    ? "Leído de la centralita."
    : "Calculado con la velocidad desde la última corrección" + (m.fKm ? ` (${fecha(m.fKm, false)})` : "") + ". Corrígelo con el cuentakilómetros de la moto.";
  $("#mant-horas").innerHTML = `${num(m.horas, 1)} <span class="uni secundario">h</span>`;
  $("#lista-mant").innerHTML = m.items.map((it, i) => {
    const e = estadoItem(it, m.km);
    return `<div class="tarjeta mant-item ${e.nivel}">
      <h3>${esc(it.nombre)} ${e.nivel ? `<span class="etiqueta ${e.nivel}">${e.falta <= 0 ? "Toca ya" : "Pronto"}</span>` : ""}</h3>
      <div class="fila-sep"><span class="secundario">${e.falta >= 0 ? "Faltan" : "Pasado por"}</span><b>${num(Math.abs(e.falta))} km</b></div>
      <div class="barra"><div style="width:${e.pct}%"></div></div>
      <div class="fila-sep" style="margin-top:10px"><span class="secundario">Horas de motor desde el cambio</span><b>${num(Math.max(0, m.horas - it.h), 1)} h</b></div>
      <div class="campo"><label for="mi-${i}">Cada (km)</label><input id="mi-${i}" data-i="${i}" data-campo="int" inputmode="numeric" value="${num(it.int)}"></div>
      <div class="campo"><label for="mk-${i}">Último cambio a (km)</label><input id="mk-${i}" data-i="${i}" data-campo="km" inputmode="numeric" value="${num(it.km)}"></div>
      <p class="secundario">${it.f ? "Marcado como hecho el " + fecha(it.f, false) : ""}</p>
      <button class="boton boton-acento ancho" data-hecho="${i}">Hecho ahora</button>
    </div>`;
  }).join("");
  $$("#lista-mant input").forEach((inp) => inp.addEventListener("change", async () => {
    const v = leerNumero(inp.value);
    if (!isFinite(v) || v < 0) { aviso("Número no válido", "error"); return; }
    const items = [{}, {}, {}];
    items[+inp.dataset.i][inp.dataset.campo] = v;
    try { await api("/api/mant", { items }); aviso("Guardado", "ok"); alEntrar.mant(); }
    catch (e) { aviso(e.message, "error"); }
  }));
  $$("[data-hecho]").forEach((b) => b.addEventListener("click", async () => {
    const it = m.items[+b.dataset.hecho];
    const ok = await confirmar({ titulo: it.nombre, html: `<p>¿Marcar como hecho ahora, a <b>${num(m.km)} km</b> y <b>${num(m.horas, 1)} h</b> de motor?</p>`, si: "Marcar hecho", peligro: false });
    if (!ok) return;
    try { await api("/api/mant", { hecho: +b.dataset.hecho }); aviso("Anotado", "ok"); alEntrar.mant(); }
    catch (e) { aviso(e.message, "error"); }
  }));
};

$("#btn-odo").addEventListener("click", async () => {
  const v = await confirmar({
    titulo: "Cuentakilómetros", html: "<p>Escribe los kilómetros que marca el cuadro de la moto.</p>",
    si: "Guardar", peligro: false, entrada: { valor: S.mant ? Math.round(S.mant.km) : "", placeholder: "km" },
  });
  if (v === null) return;
  const km = leerNumero(v);
  if (!isFinite(km)) { aviso("Número no válido", "error"); return; }
  try { await api("/api/mant", { km }); aviso("Kilómetros corregidos", "ok"); alEntrar.mant(); }
  catch (e) { aviso(e.message, "error"); }
});

// ------------------------------------------------------------------ centralita

alEntrar.ecu = async () => {
  try {
    const e = await api("/api/ecu");
    const fila = (t, v) => `<div class="tarjeta ecu-dato"><span class="etq">${t}</span><b>${esc(v || "No disponible")}</b></div>`;
    $("#lista-ecu").innerHTML = e.conexion !== 2 ? `<p class="vacio">Sin conexión con la centralita.</p>` :
      fila("VIN (número de bastidor)", e.vin) +
      fila("Versión de software (calibración)", e.calid) +
      fila("Protocolo detectado", e.protocolo) +
      fila("Dirección", e.direccion) +
      fila("Valores disponibles", `${e.pidsConocidos} (la centralita declara ${e.pidsAdmitidos})`) +
      fila("Programa del aparato", "v" + e.version);
  } catch (e) { aviso(e.message, "error"); }
};

// ------------------------------------------------------------------ ajustes

let tAjuste;
alEntrar.ajustes = async () => {
  try {
    const a = await api("/api/ajustes");
    S.tl = a.tempLimite;
    $("#tl-valor").textContent = `${num(a.tempLimite)} °C`;
    $("#sw-sim").checked = a.simulador;
    $("#bat-limite").textContent = `${num(S.batLimite, 1)} V`;
  } catch (e) { aviso(e.message, "error"); }
};
function cambiarLimite(d) {
  S.tl = Math.max(60, Math.min(130, (S.tl || 100) + d));
  $("#tl-valor").textContent = `${num(S.tl)} °C`;
  clearTimeout(tAjuste);
  tAjuste = setTimeout(() => api("/api/ajustes", { tempLimite: S.tl }).then(() => aviso("Límite guardado", "ok")).catch((e) => aviso(e.message, "error")), 600);
}
$("#tl-menos").addEventListener("click", () => cambiarLimite(-1));
$("#tl-mas").addEventListener("click", () => cambiarLimite(1));
$("#sw-sim").addEventListener("change", async (ev) => {
  try {
    await api("/api/ajustes", { simulador: ev.target.checked });
    aviso(ev.target.checked ? "Modo simulador activado" : "Modo moto: buscando la centralita", "ok");
    S.pids = []; S.pidsCargados = false; S.averias = null; S.dv = -1;
    $("#lista-datos").innerHTML = "";
    setTimeout(cargarMant, 1000);
  } catch (e) { aviso(e.message, "error"); ev.target.checked = !ev.target.checked; }
});

// ------------------------------------------------------------------ OTA

let archivoOta = null;
let tipoOta = null;   // "programa" o "web"
$("#ota-archivo").addEventListener("change", (ev) => {
  archivoOta = ev.target.files[0] || null;
  tipoOta = null;
  if (archivoOta) {
    const n = archivoOta.name.toLowerCase();
    tipoOta = n.endsWith(".bin") ? "programa" : n.endsWith(".tar") ? "web" : null;
  }
  $("#ota-nombre").textContent = archivoOta ? `${archivoOta.name} (${tamano(archivoOta.size)})` : "Elegir archivo";
  $("#ota-estado").textContent = archivoOta && !tipoOta ? "Ese archivo no vale: tiene que ser firmware.bin o web.tar." :
    tipoOta === "web" ? "Se actualizarán las pantallas." : tipoOta === "programa" ? "Se actualizará el programa." : "";
  $("#btn-ota").disabled = !tipoOta;
});
alEntrar.ota = async () => {
  try { const e = await api("/api/estado"); $("#ota-version").textContent = "v" + e.version; } catch (e) { /* */ }
};
$("#btn-ota").addEventListener("click", async () => {
  if (!archivoOta) return;
  const esWeb = tipoOta === "web";
  const ok = await confirmar({
    titulo: esWeb ? "Actualizar pantallas" : "Actualizar programa",
    html: esWeb ? `<p>Se instalarán las pantallas de <b>${esc(archivoOta.name)}</b>. Tus datos se conservan.</p>`
                : `<p>Se instalará <b>${esc(archivoOta.name)}</b> y el aparato se reiniciará. Tus datos se conservan.</p>`,
    si: "Actualizar", peligro: false,
  });
  if (!ok) return;
  const fd = new FormData();
  fd.append(esWeb ? "web" : "firmware", archivoOta, archivoOta.name);
  const xhr = new XMLHttpRequest();
  $("#btn-ota").disabled = true;
  $("#ota-estado").textContent = "Subiendo…";
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const p = 100 * e.loaded / e.total;
      $("#ota-barra").style.width = p.toFixed(0) + "%";
      $("#ota-estado").textContent = `Subiendo… ${num(p)} %`;
    }
  };
  xhr.onload = () => {
    let j = {};
    try { j = JSON.parse(xhr.responseText); } catch (e) { /* */ }
    if (xhr.status === 200 && j.ok) {
      $("#ota-estado").textContent = esWeb ? "Pantallas actualizadas. Recargando…" : "Actualizado. El aparato se reinicia; la página se recargará sola.";
      setTimeout(() => location.reload(), esWeb ? 1500 : 9000);
    } else {
      $("#ota-estado").textContent = "Error: " + (j.msg || xhr.status);
      $("#btn-ota").disabled = false;
    }
  };
  xhr.onerror = () => { $("#ota-estado").textContent = "Se ha perdido la conexión durante la subida."; $("#btn-ota").disabled = false; };
  xhr.open("POST", esWeb ? "/api/ota-web" : "/api/ota");
  xhr.send(fd);
});

// ------------------------------------------------------------------ informe PDF

const INF = { blob: null, nombre: "" };

$("#btn-pdf").addEventListener("click", async () => {
  const est = $("#pdf-estado");
  est.textContent = "Preparando el informe…";
  try {
    const [av, ecu, hist, mant, rec] = await Promise.all([
      api("/api/averias"), api("/api/ecu"), api("/api/historial"), api("/api/mant"), api("/api/records"),
    ]);
    const datos = await api("/api/pids").catch(() => ({ pids: [] }));
    const ahora = Date.now() / 1000;
    const bloques = bloquesInforme({ av, ecu, hist, mant, rec, pids: datos.pids || [], vivo: S.vivo });
    INF.blob = new Blob([pdfInforme(bloques, ahora)], { type: "application/pdf" });
    const d = new Date();
    INF.nombre = `informe-nmax-${d.getFullYear()}${dos(d.getMonth() + 1)}${dos(d.getDate())}-${dos(d.getHours())}${dos(d.getMinutes())}.pdf`;
    $("#previa-hoja").innerHTML = htmlInforme(bloques, ahora);
    $("#previa").classList.remove("oculto");
    $("#previa-cuerpo").scrollTop = 0;
    est.textContent = "";
  } catch (e) {
    est.textContent = "No se ha podido generar: " + e.message;
  }
});

$("#previa-cerrar").addEventListener("click", () => $("#previa").classList.add("oculto"));
$("#previa-descargar").addEventListener("click", () => {
  if (!INF.blob) return;
  const url = URL.createObjectURL(INF.blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = INF.nombre;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
  aviso("PDF guardado en Archivos → Descargas", "ok");
});

const COL_INF = { rojo: "#C0392B", naranja: "#B9770E", verde: "#1E8449", gris: "#5A6573", azul: "#21409A" };

// Contenido del informe como lista de bloques; sirve para la vista previa y para el PDF
function bloquesInforme({ av, ecu, hist, mant, rec, pids, vivo }) {
  const B = [];
  const sec = (txt) => B.push({ t: "seccion", txt });
  const par = (k, v, color) => B.push({ t: "par", k, v, color });
  const sub = (txt) => B.push({ t: "sub", txt });
  const dtc = (cod, extra, color) => B.push({ t: "dtc", cod, desc: descripcionDtc(cod), extra, color });
  const tabla = (filas) => B.push({ t: "tabla", filas });
  const nada = (txt) => B.push({ t: "parrafo", txt, color: "gris" });

  sec("Moto y centralita");
  par("VIN", ecu.vin || "No disponible");
  par("Versión de software", ecu.calid || "No disponible");
  par("Protocolo", ecu.protocolo || "—");
  par("Dirección", ecu.direccion || "—");
  par("Valores disponibles", `${ecu.pidsConocidos ?? 0} (declarados ${ecu.pidsAdmitidos ?? 0})`);
  par("Cuentakilómetros", `${num(mant.km)} km${mant.kmCentralita ? " (centralita)" : " (estimado)"}`);
  par("Horas de motor", `${num(mant.horas, 1)} h`);
  if (vivo && vivo.s) par("Atención", "Datos generados por el MODO SIMULADOR, no por la moto.", "rojo");

  sec("Testigo del motor y averías");
  par("Testigo del motor", !av.milConocido ? "Desconocido" : av.mil ? "ENCENDIDO" : "Apagado", av.mil ? "naranja" : "verde");
  sub("Averías guardadas");
  if (av.guardadas.length) av.guardadas.forEach((c) => dtc(c, av.foto && av.foto.valida && av.foto.dtc === c ? "Con foto del fallo" : "", "rojo"));
  else nada("Ninguna");
  sub("Averías pendientes");
  if (av.pendientes.length) av.pendientes.forEach((c) => dtc(c, "", "naranja"));
  else nada("Ninguna");

  if (av.foto && av.foto.valida) {
    sec(`Foto del fallo (${av.foto.dtc})`);
    tabla((av.foto.valores || []).map((p) => [p.nombre, `${num(p.v, p.dec)} ${p.unidad}`]));
  }
  if (vivo && vivo.c === 2 && pids.length) {
    sec("Valores en el momento del informe");
    tabla(pids.filter((p) => vivo.v[p.id] !== undefined).map((p) => [p.nombre, `${num(vivo.v[p.id], p.dec)} ${p.unidad}`]));
  }

  sec("Mantenimiento");
  for (const it of mant.items) {
    const e = estadoItem(it, mant.km);
    par(it.nombre, `${e.falta >= 0 ? "Faltan" : "Pasado por"} ${num(Math.abs(e.falta))} km · cada ${num(it.int)} km · último a ${num(it.km)} km`,
      e.nivel === "peligro" ? "rojo" : e.nivel === "aviso" ? "naranja" : null);
  }

  sec("Récords");
  tabla([
    ["Velocidad máxima", rec.vmax ? `${num(rec.vmax)} km/h` : "—"],
    ["Revoluciones máximas", rec.rpmMax ? `${num(rec.rpmMax)} rpm` : "—"],
    ["0 a 50 km/h", rec.t050 ? `${num(rec.t050, 2)} s` : "—"],
  ]);

  sec("Historial de averías");
  const h = (hist.h || []).slice().sort((a, b) => (b.d || 0) - (a.d || 0)).slice(0, 40);
  if (!h.length) nada("Sin registros.");
  for (const e of h) dtc(e.c, `Detectada ${fecha(e.d)}${e.e === "b" ? ` · borrada ${fecha(e.h)} (${e.m || ""})` : " · ACTIVA"}`, e.e === "a" ? "rojo" : "gris");
  return B;
}

const TITULO_INF = "Informe de diagnosis · Yamaha NMAX 125";
const subtituloInf = (t) => "GPD125-A (BALD) · generado el " + fecha(t);

function htmlInforme(B, t) {
  const c = (k) => (k ? ` style="color:${COL_INF[k]}"` : "");
  let h = `<div class="inf-cab"><b>${esc(TITULO_INF)}</b><span>${esc(subtituloInf(t))}</span></div>`;
  for (const b of B) {
    if (b.t === "seccion") h += `<h4>${esc(b.txt)}</h4>`;
    else if (b.t === "sub") h += `<h5>${esc(b.txt)}</h5>`;
    else if (b.t === "par") h += `<div class="inf-par"><span>${esc(b.k)}</span><b${c(b.color)}>${esc(b.v)}</b></div>`;
    else if (b.t === "parrafo") h += `<p${c(b.color)}>${esc(b.txt)}</p>`;
    else if (b.t === "dtc") h += `<div class="inf-dtc"><b${c(b.color)}>${esc(b.cod)}</b><span>${esc(b.desc)}${b.extra ? `<small>${esc(b.extra)}</small>` : ""}</span></div>`;
    else if (b.t === "tabla") h += `<div class="inf-tabla">${b.filas.map(([n, v]) => `<div><span>${esc(n)}</span><b>${esc(v)}</b></div>`).join("")}</div>`;
  }
  return h;
}

function pdfInforme(B, t) {
  const pdf = new Pdf();
  const M = 46, ANCHO = pdf.W - 2 * M;
  let y = 0;
  const col = (k) => (k ? COL_INF[k] : "#111111");
  const GRIS = COL_INF.gris, AZUL = COL_INF.azul;
  const cabecera = () => {
    pdf.rect(0, 0, pdf.W, 64, AZUL);
    pdf.texto(M, 30, TITULO_INF, { tam: 17, negrita: true, color: "#FFFFFF" });
    pdf.texto(M, 50, subtituloInf(t), { tam: 10, color: "#DDE3F5" });
    y = 92;
  };
  const espacio = (h) => { if (y + h > pdf.H - 50) { pdf.nuevaPagina(); cabecera(); } };

  cabecera();
  for (const b of B) {
    if (b.t === "seccion") {
      espacio(40);
      y += 8;
      pdf.texto(M, y, b.txt, { tam: 13, negrita: true, color: AZUL });
      y += 6;
      pdf.linea(M, y, M + ANCHO, y, AZUL, 1);
      y += 16;
    } else if (b.t === "sub") {
      espacio(30);
      y += 4;
      pdf.texto(M, y, b.txt, { tam: 11, negrita: true });
      y += 16;
    } else if (b.t === "par") {
      const lineas = pdf.partir(b.v, ANCHO - 170, 10, true);
      espacio(13 * lineas.length + 3);
      pdf.texto(M, y, b.k, { tam: 10, color: GRIS });
      lineas.forEach((l, i) => pdf.texto(M + 170, y + i * 13, l, { tam: 10, negrita: true, color: col(b.color) }));
      y += 13 * lineas.length + 3;
    } else if (b.t === "parrafo") {
      for (const l of pdf.partir(b.txt, ANCHO, 10, false)) {
        espacio(14);
        pdf.texto(M, y, l, { tam: 10, color: col(b.color) });
        y += 13;
      }
    } else if (b.t === "dtc") {
      const lineas = pdf.partir(b.desc, ANCHO - 70, 10, false);
      const extra = b.extra ? pdf.partir(b.extra, ANCHO - 70, 9, false) : [];
      espacio(13 * lineas.length + 12 * extra.length + 8);
      pdf.texto(M, y, b.cod, { tam: 12, negrita: true, color: col(b.color) });
      lineas.forEach((l, i) => pdf.texto(M + 70, y + i * 13, l, { tam: 10 }));
      y += 13 * lineas.length;
      extra.forEach((l) => { pdf.texto(M + 70, y, l, { tam: 9, color: GRIS }); y += 12; });
      y += 6;
    } else if (b.t === "tabla") {
      const ancho = ANCHO / 2;
      for (let i = 0; i < b.filas.length; i += 2) {
        espacio(16);
        for (let k = 0; k < 2 && i + k < b.filas.length; k++) {
          const [n, v] = b.filas[i + k];
          const x = M + k * ancho;
          pdf.texto(x, y, n, { tam: 9.5, color: GRIS });
          pdf.texto(x + ancho - 12, y, v, { tam: 10, negrita: true, alinear: "derecha" });
        }
        y += 15;
      }
    }
  }
  const n = pdf.paginas.length;
  pdf.paginas.forEach((ops, i) => {
    pdf.ops = ops;
    pdf.texto(pdf.W - M, pdf.H - 24, `Página ${i + 1} de ${n}`, { tam: 8, color: GRIS, alinear: "derecha" });
    pdf.texto(M, pdf.H - 24, "Aparato de diagnosis ESP32 · línea K / OBD2", { tam: 8, color: GRIS });
  });
  return pdf.generar();
}

// ------------------------------------------------------------------ arranque

setTimeout(() => {
  const sp = $("#splash");
  sp.classList.add("fuera");
  setTimeout(() => sp.remove(), 500);
}, 3000);
pintarEstado();
conectarWs();
