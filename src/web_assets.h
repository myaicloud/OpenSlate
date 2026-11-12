// ============================================================================
// web_assets.h — Embedded OpenSlate Web UI (PROGMEM)
// ============================================================================
// Single-page app served at http://192.168.4.1 when connected to the
// OpenSlate Wi-Fi network. Fully self-contained (captive portal = no
// internet): no CDN, no web fonts, inline SVG icon sprite.
//
//   • Views: Live / Camera / Switch / OSD / System (hash-routed)
//   • Bottom tab bar on phones, sidebar on desktop
//   • Follows the system light/dark theme, manual override in localStorage
//   • Settings auto-save on change (Wi-Fi credentials need explicit confirm)
//   • Controls that the firmware refuses while armed are locked in the UI
//   • One 1.5 s poll loop on /api/status (5 s while the tab is hidden)
// ============================================================================

#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark light">
<meta name="theme-color" content="#0c0e12">
<title>OpenSlate</title>
<style>
:root{
  --bg:#f3f4f7;--bg2:#ffffff;--surf:#ffffff;--surf2:#f1f2f5;--line:#e2e4ea;--line2:#d3d6de;
  --txt:#14171c;--dim:#5f6672;--faint:#9097a3;
  --acc:#4a5bd8;--acc-t:#fff;--acc-s:rgba(74,91,216,.12);
  --ok:#14945a;--ok-s:rgba(20,148,90,.12);--warn:#b86e00;--warn-s:rgba(184,110,0,.12);
  --rec:#e0283a;--rec-s:rgba(224,40,58,.11);
  --sh:0 1px 2px rgba(16,24,40,.05),0 1px 3px rgba(16,24,40,.06);
  --osd-bg:linear-gradient(160deg,#3c4a5c,#1d2530 60%,#2b2a24);
  --r:14px;--r2:10px;
  --font:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;
  --mono:ui-monospace,'SF Mono','Cascadia Mono',Consolas,monospace;
}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  --bg:#0c0e12;--bg2:#101318;--surf:#15181e;--surf2:#1c2027;--line:#252a33;--line2:#323844;
  --txt:#e9ebef;--dim:#9aa1ad;--faint:#666e7b;
  --acc:#8c9bff;--acc-t:#0c0e12;--acc-s:rgba(140,155,255,.14);
  --ok:#3dd68c;--ok-s:rgba(61,214,140,.13);--warn:#f5b041;--warn-s:rgba(245,176,65,.13);
  --rec:#ff4d5e;--rec-s:rgba(255,77,94,.14);--sh:none}}
:root[data-theme=dark]{
  --bg:#0c0e12;--bg2:#101318;--surf:#15181e;--surf2:#1c2027;--line:#252a33;--line2:#323844;
  --txt:#e9ebef;--dim:#9aa1ad;--faint:#666e7b;
  --acc:#8c9bff;--acc-t:#0c0e12;--acc-s:rgba(140,155,255,.14);
  --ok:#3dd68c;--ok-s:rgba(61,214,140,.13);--warn:#f5b041;--warn-s:rgba(245,176,65,.13);
  --rec:#ff4d5e;--rec-s:rgba(255,77,94,.14);--sh:none}
*,*:before,*:after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;font:15px/1.45 var(--font);background:var(--bg);color:var(--txt);-webkit-tap-highlight-color:transparent}
button,input,select{font:inherit;color:inherit}
h1,h2,h3,p{margin:0}
.ic{width:20px;height:20px;flex:none;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
.num{font-variant-numeric:tabular-nums}
.mono{font-family:var(--mono)}
.dim{color:var(--dim)}
[hidden]{display:none!important}

/* ---------- shell ---------- */
.app{min-height:100vh;display:grid;grid-template-columns:1fr}
.nav{position:fixed;z-index:20;left:0;right:0;bottom:0;display:flex;background:var(--bg2);border-top:1px solid var(--line);
  padding:6px 6px calc(6px + env(safe-area-inset-bottom))}
.nav .brand{display:none}
.nav button{flex:1;display:flex;flex-direction:column;align-items:center;gap:3px;padding:7px 2px;border:0;background:none;
  color:var(--faint);font-size:11px;font-weight:600;border-radius:var(--r2);cursor:pointer}
.nav button .ic{width:22px;height:22px}
.nav button[aria-current=page]{color:var(--acc)}
.main{min-width:0;padding:0 16px calc(88px + env(safe-area-inset-bottom))}
.top{position:sticky;top:0;z-index:10;display:flex;align-items:center;gap:10px;height:60px;margin:0 -16px;padding:0 16px;
  background:color-mix(in srgb,var(--bg) 88%,transparent);-webkit-backdrop-filter:blur(12px);backdrop-filter:blur(12px)}
.top h1{flex:1;min-width:0;font-size:19px;font-weight:700;letter-spacing:-.01em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.logo{display:flex;align-items:center;gap:9px;font-weight:750;font-size:17px;letter-spacing:-.01em}
.logo svg{width:28px;height:28px}
.wrap{max-width:880px;margin:0 auto}
@media (min-width:900px){
  .app{grid-template-columns:232px 1fr}
  .nav{position:sticky;top:0;height:100vh;flex-direction:column;gap:2px;border-top:0;border-right:1px solid var(--line);padding:18px 12px}
  .nav .brand{display:flex;padding:4px 10px 22px}
  .nav button{flex:none;flex-direction:row;gap:12px;padding:10px 12px;font-size:14.5px;color:var(--dim)}
  .nav button .ic{width:20px;height:20px}
  .nav button:hover{background:var(--surf2);color:var(--txt)}
  .nav button[aria-current=page]{background:var(--acc-s);color:var(--acc)}
  .nav .foot{margin-top:auto;padding:10px 12px;font-size:12px;color:var(--faint)}
  .main{padding:0 32px 48px}.top{margin:0 -32px;padding:0 32px;height:72px}
  .top .logo{display:none}
}
.nav .foot{display:none}@media (min-width:900px){.nav .foot{display:block}}

/* ---------- status pill + icon button ---------- */
.pill{display:inline-flex;align-items:center;gap:7px;height:32px;padding:0 12px;border-radius:99px;font-size:12.5px;font-weight:650;
  background:var(--surf);border:1px solid var(--line);white-space:nowrap}
.pill i{width:8px;height:8px;border-radius:50%;background:var(--faint)}
.pill.ok i{background:var(--ok)}.pill.warn i{background:var(--warn)}
.pill.rec{background:var(--rec);border-color:var(--rec);color:#fff}.pill.rec i{background:#fff;animation:blink 1s steps(2) infinite}
@keyframes blink{50%{opacity:.25}}
.ibtn{width:36px;height:36px;display:grid;place-items:center;border-radius:10px;border:1px solid var(--line);background:var(--surf);cursor:pointer;color:var(--dim)}
.ibtn:hover{color:var(--txt)}

/* ---------- banners ---------- */
.banner{display:flex;gap:10px;align-items:flex-start;padding:11px 14px;border-radius:var(--r2);font-size:13.5px;margin-bottom:14px;
  background:var(--warn-s);color:var(--txt);border:1px solid color-mix(in srgb,var(--warn) 35%,transparent)}
.banner .ic{color:var(--warn);margin-top:1px}
.banner.err{background:var(--rec-s);border-color:color-mix(in srgb,var(--rec) 35%,transparent)}.banner.err .ic{color:var(--rec)}

/* ---------- cards ---------- */
.view{display:grid;gap:16px;padding-top:4px;animation:fade .25s ease}
@keyframes fade{from{opacity:0;transform:translateY(4px)}}
.card{background:var(--surf);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--sh);padding:18px}
.card>header{display:flex;align-items:center;gap:10px;margin-bottom:14px}
.card>header h2{flex:1;font-size:15px;font-weight:650}
.card>header .meta{font-size:12.5px;color:var(--dim)}
.sub{font-size:13px;color:var(--dim);margin-top:-8px;margin-bottom:14px}
.grid{display:grid;gap:12px;grid-template-columns:repeat(2,minmax(0,1fr))}
@media (min-width:700px){.grid.g4{grid-template-columns:repeat(4,minmax(0,1fr))}.grid.g3{grid-template-columns:repeat(3,minmax(0,1fr))}}
.cols{display:grid;gap:16px}@media (min-width:1100px){.cols{grid-template-columns:1fr 1fr;align-items:start}}

/* ---------- stat tiles ---------- */
.stat{background:var(--surf);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--sh);padding:14px 15px;min-width:0}
.card .stat{background:var(--surf2);border:0;box-shadow:none}
.stat .k{display:flex;align-items:center;gap:7px;font-size:12px;font-weight:600;color:var(--dim);margin-bottom:8px}
.stat .k .ic{width:16px;height:16px}
.stat .v{font-size:22px;font-weight:700;letter-spacing:-.01em;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat .v small{font-size:13px;font-weight:600;color:var(--dim);margin-left:2px}
.stat .h{font-size:12px;color:var(--dim);margin-top:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.meter{height:5px;border-radius:9px;background:var(--surf2);margin-top:10px;overflow:hidden}
.meter i{display:block;height:100%;width:0;border-radius:9px;background:var(--ok);transition:width .6s}
.card .meter{background:var(--line)}

/* ---------- tags ---------- */
.tag{display:inline-flex;align-items:center;gap:5px;height:22px;padding:0 8px;border-radius:6px;font-size:11.5px;font-weight:700;
  letter-spacing:.02em;background:var(--surf2);color:var(--dim);white-space:nowrap}
.tag.ok{background:var(--ok-s);color:var(--ok)}.tag.warn{background:var(--warn-s);color:var(--warn)}
.tag.rec{background:var(--rec-s);color:var(--rec)}.tag.acc{background:var(--acc-s);color:var(--acc)}

/* ---------- record hero ---------- */
.hero{display:grid;gap:18px;align-items:center;padding:22px}
@media (min-width:700px){.hero{grid-template-columns:1fr auto;padding:26px 28px}}
.hero .state{display:flex;align-items:center;gap:8px;margin-bottom:6px}
.timer{font:700 clamp(52px,14vw,76px)/1 var(--mono);letter-spacing:-.03em;font-variant-numeric:tabular-nums}
.hero.recording .timer{color:var(--rec)}
.hero.down .timer{color:var(--faint)}
.hero .info{font-size:13.5px;color:var(--dim);margin-top:10px;max-width:420px}
.recbtn{justify-self:center;width:132px;height:132px;border-radius:50%;border:0;cursor:pointer;display:grid;place-items:center;
  background:var(--surf2);box-shadow:inset 0 0 0 1px var(--line2);transition:transform .12s,box-shadow .2s}
.recbtn span{display:block;width:62px;height:62px;border-radius:50%;background:var(--rec);transition:all .25s cubic-bezier(.3,.7,.3,1)}
.recbtn:active:not(:disabled){transform:scale(.95)}
.recbtn:not(:disabled):hover{box-shadow:inset 0 0 0 1px var(--line2),0 0 0 6px var(--rec-s)}
.recbtn.on{box-shadow:inset 0 0 0 3px var(--rec)}
.recbtn.on span{width:44px;height:44px;border-radius:9px}
.recbtn:disabled{cursor:not-allowed}.recbtn:disabled span{background:var(--line2)}
.reclbl{text-align:center;font-size:12px;font-weight:700;letter-spacing:.08em;color:var(--dim);margin-top:10px;text-transform:uppercase}

/* ---------- OSD goggle preview ---------- */
.osd{position:relative;border-radius:var(--r2);background:var(--osd-bg);overflow:hidden;
  display:flex;flex-direction:column;justify-content:center;gap:6px;padding:22px 20px}
.osd:after{content:'';position:absolute;inset:0;background:repeating-linear-gradient(0deg,rgba(255,255,255,.025) 0 1px,transparent 1px 3px);pointer-events:none}
.osd div{display:flex;gap:12px;font:600 14px/1.25 var(--mono);color:#fff;text-shadow:0 0 2px #000,0 0 2px #000;white-space:pre}
.osd div b{color:rgba(255,255,255,.45);font-weight:500;font-size:11px;align-self:center;width:28px}
.osd div.off span{color:rgba(255,255,255,.3)}

/* ---------- forms ---------- */
.field{display:grid;gap:7px;margin-bottom:16px}
.field:last-child{margin-bottom:0}
.field>label,.lab{display:flex;align-items:baseline;justify-content:space-between;gap:10px;font-size:13.5px;font-weight:600}
.field>label output,.lab output{font:600 13px var(--mono);color:var(--acc)}
.hint{font-size:12.5px;color:var(--dim)}
select,input[type=text],input[type=password]{width:100%;height:42px;padding:0 12px;border-radius:var(--r2);border:1px solid var(--line2);
  background:var(--bg2);outline:none;transition:border-color .15s,box-shadow .15s}
select{appearance:none;-webkit-appearance:none;padding-right:34px;cursor:pointer;
  background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' fill='none' stroke='%23888' stroke-width='1.8' stroke-linecap='round'%3E%3Cpath d='M1 1.5l5 5 5-5'/%3E%3C/svg%3E");
  background-repeat:no-repeat;background-position:right 12px center}
select:focus,input:focus{border-color:var(--acc);box-shadow:0 0 0 3px var(--acc-s)}
select:disabled,input:disabled{opacity:.5;cursor:not-allowed}
.pw{position:relative}.pw input{padding-right:44px}
.pw button{position:absolute;right:4px;top:4px;width:34px;height:34px;border:0;background:none;color:var(--dim);cursor:pointer;display:grid;place-items:center}

/* range with live-RC marker */
.rng{position:relative;height:28px;display:flex;align-items:center}
.rng input{position:relative;z-index:1;width:100%;height:28px;margin:0;-webkit-appearance:none;appearance:none;background:transparent;cursor:pointer}
.rng input::-webkit-slider-runnable-track{height:6px;border-radius:9px;background:linear-gradient(90deg,var(--acc) var(--p,50%),var(--line2) var(--p,50%))}
.rng input::-moz-range-track{height:6px;border-radius:9px;background:linear-gradient(90deg,var(--acc) var(--p,50%),var(--line2) var(--p,50%))}
.rng input::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;margin-top:-8px;border-radius:50%;background:#fff;border:0;box-shadow:0 0 0 1px rgba(0,0,0,.15),0 2px 6px rgba(0,0,0,.3)}
.rng input::-moz-range-thumb{width:22px;height:22px;border-radius:50%;background:#fff;border:0;box-shadow:0 0 0 1px rgba(0,0,0,.15),0 2px 6px rgba(0,0,0,.3)}
.rng .mk{position:absolute;z-index:2;top:1px;width:3px;height:26px;margin-left:-1.5px;border-radius:2px;background:var(--warn);pointer-events:none;transition:left .3s}
.rng .mk.on{background:var(--rec)}
.scale{display:flex;justify-content:space-between;font:11px var(--mono);color:var(--faint)}

/* live RC readout */
.live{display:flex;align-items:center;gap:14px;padding:12px 14px;border-radius:var(--r2);background:var(--surf2);margin-bottom:16px}
.live .v{font:700 24px var(--mono);font-variant-numeric:tabular-nums}
.live .v small{font-size:13px;color:var(--dim)}
.live .sp{flex:1}

/* toggles */
.row{display:flex;align-items:center;gap:14px;padding:13px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0;padding-top:0}
.row .t{flex:1;min-width:0}.row .t b{display:block;font-size:14.5px;font-weight:600}.row .t p{font-size:12.5px;color:var(--dim);margin-top:2px}
.row.off{opacity:.5}
.tg{position:relative;width:46px;height:28px;flex:none}
.tg input{position:absolute;inset:0;opacity:0;margin:0;cursor:pointer;z-index:1}
.tg i{position:absolute;inset:0;border-radius:99px;background:var(--line2);transition:background .2s}
.tg i:after{content:'';position:absolute;left:3px;top:3px;width:22px;height:22px;border-radius:50%;background:#fff;box-shadow:0 1px 3px rgba(0,0,0,.3);transition:transform .2s}
.tg input:checked+i{background:var(--acc)}
.tg input:checked+i:after{transform:translateX(18px)}
.tg input:focus-visible+i{box-shadow:0 0 0 3px var(--acc-s)}
.tg input:disabled{cursor:not-allowed}

/* buttons */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;height:40px;padding:0 16px;border-radius:var(--r2);
  border:1px solid var(--line2);background:var(--surf);font-size:14px;font-weight:600;cursor:pointer;white-space:nowrap;transition:background .15s,opacity .15s}
.btn:hover:not(:disabled){background:var(--surf2)}
.btn .ic{width:18px;height:18px}
.btn.pri{background:var(--acc);border-color:var(--acc);color:var(--acc-t)}
.btn.pri:hover:not(:disabled){background:var(--acc);filter:brightness(1.08)}
.btn.dng{color:var(--rec);border-color:color-mix(in srgb,var(--rec) 40%,transparent)}
.btn.dng:hover:not(:disabled){background:var(--rec-s)}
.btn.sm{height:32px;padding:0 12px;font-size:13px}
.btn.ghost{border-color:transparent;background:none;color:var(--dim)}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn.busy{pointer-events:none;opacity:.7}
.btn.busy .ic{animation:spin 1s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.acts{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:16px}

/* segmented */
.seg{display:inline-flex;padding:3px;border-radius:11px;background:var(--surf2);gap:2px}
.seg button{height:34px;padding:0 16px;border:0;border-radius:8px;background:none;font-size:13.5px;font-weight:600;color:var(--dim);cursor:pointer}
.seg button[aria-pressed=true]{background:var(--surf);color:var(--txt);box-shadow:0 1px 3px rgba(0,0,0,.18)}
.seg button:disabled{cursor:not-allowed;opacity:.5}

/* list items (cameras) */
.list{display:grid;gap:8px}
.item{display:flex;align-items:center;gap:12px;padding:11px 12px;border-radius:var(--r2);background:var(--surf2);min-width:0}
.item .av{width:40px;height:40px;border-radius:10px;display:grid;place-items:center;background:var(--surf);color:var(--dim);flex:none;
  font:800 11px var(--font);letter-spacing:.02em}
.item .av.dji{color:var(--acc)}.item .av.gp{color:var(--txt)}
.item .t{flex:1;min-width:0}
.item .t b{display:block;font-size:14.5px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item .t span{display:block;font:12px var(--mono);color:var(--dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item .x{display:flex;gap:6px;align-items:center;flex:none}
.item.active{background:var(--acc-s);box-shadow:inset 0 0 0 1px color-mix(in srgb,var(--acc) 40%,transparent)}
.empty{padding:22px 12px;text-align:center;font-size:13.5px;color:var(--dim);border:1px dashed var(--line2);border-radius:var(--r2)}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}
.bars i{width:3px;border-radius:1px;background:var(--line2)}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:14px}
.bars.b1 i:nth-child(-n+1){background:var(--rec)}.bars.b2 i:nth-child(-n+2){background:var(--warn)}
.bars.b3 i:nth-child(-n+3){background:var(--ok)}.bars.b4 i{background:var(--ok)}

/* active camera */
.cam{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.cam .av{width:52px;height:52px;border-radius:13px;display:grid;place-items:center;background:var(--surf2);font:800 12px var(--font);color:var(--acc)}
.cam .t{flex:1;min-width:160px}.cam .t b{display:block;font-size:17px;font-weight:650}
.cam .t span{font:12.5px var(--mono);color:var(--dim)}

/* scan progress */
.prog{height:3px;border-radius:3px;background:var(--line);overflow:hidden;margin-top:14px}
.prog i{display:block;height:100%;width:0;background:var(--acc);transition:width .25s linear}
.steps{margin:14px 0 0;padding:0;list-style:none;display:grid;gap:8px;counter-reset:s}
.steps li{display:flex;gap:10px;font-size:13px;color:var(--dim)}
.steps li:before{counter-increment:s;content:counter(s);flex:none;width:20px;height:20px;border-radius:50%;background:var(--surf2);
  color:var(--txt);font-size:11px;font-weight:700;display:grid;place-items:center}
details{border-top:1px solid var(--line);margin-top:16px;padding-top:12px}
summary{cursor:pointer;font-size:13px;font-weight:600;color:var(--dim);list-style:none;display:flex;align-items:center;gap:6px}
summary::-webkit-details-marker{display:none}
summary:before{content:'';width:6px;height:6px;border:solid currentColor;border-width:0 1.8px 1.8px 0;transform:rotate(-45deg);transition:transform .15s;margin-right:4px}
details[open] summary:before{transform:rotate(45deg)}

/* OSD slot rows */
.slot{display:grid;grid-template-columns:44px 1fr;gap:6px 12px;align-items:center;padding:12px 0;border-top:1px solid var(--line)}
.slot:first-child{border-top:0;padding-top:0}
.slot b{font:700 13px var(--mono);color:var(--dim)}
.slot .now{grid-column:2;font:12.5px var(--mono);color:var(--dim)}

/* MSP console */
pre{margin:14px 0 0;padding:14px;border-radius:var(--r2);background:var(--bg);border:1px solid var(--line);font:12px/1.6 var(--mono);
  overflow-x:auto;white-space:pre;color:var(--dim);min-height:64px}

/* dropzone */
.drop{display:flex;flex-direction:column;align-items:center;gap:6px;padding:24px 16px;border:1.5px dashed var(--line2);border-radius:var(--r2);
  text-align:center;cursor:pointer;transition:border-color .15s,background .15s}
.drop:hover,.drop.over{border-color:var(--acc);background:var(--acc-s)}
.drop .ic{width:28px;height:28px;color:var(--acc)}
.drop b{font-size:14px}.drop span{font-size:12.5px;color:var(--dim)}
.drop input{display:none}

/* toast */
#toast{position:fixed;z-index:50;left:50%;bottom:calc(84px + env(safe-area-inset-bottom));transform:translate(-50%,16px);opacity:0;
  display:flex;align-items:center;gap:10px;max-width:min(92vw,440px);padding:11px 16px;border-radius:12px;font-size:14px;font-weight:550;
  background:#1f232b;color:#f2f3f5;box-shadow:0 8px 30px rgba(0,0,0,.3);transition:opacity .2s,transform .2s;pointer-events:none}
#toast.show{opacity:1;transform:translate(-50%,0)}
#toast i{width:8px;height:8px;border-radius:50%;flex:none;background:#8c9bff}
#toast.ok i{background:#3dd68c}#toast.err i{background:#ff4d5e}
@media (min-width:900px){#toast{bottom:28px;left:calc(50% + 116px)}}

/* dialog */
dialog{border:1px solid var(--line);border-radius:16px;padding:22px;width:min(92vw,400px);background:var(--surf);color:var(--txt);box-shadow:0 20px 60px rgba(0,0,0,.35)}
dialog::backdrop{background:rgba(5,7,10,.55)}
dialog h3{font-size:17px;margin-bottom:8px}dialog p{font-size:14px;color:var(--dim)}
dialog .acts{justify-content:flex-end;margin-top:22px}
footer.ft{text-align:center;font-size:12px;color:var(--faint);padding:24px 0 0}
@media (min-width:900px){footer.ft{display:none}}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>
<symbol id="i-logo" viewBox="0 0 32 32"><rect x="3" y="12" width="26" height="17" rx="3.5" fill="var(--acc)"/><path d="M3.6 10.6 2.8 7.3a2 2 0 0 1 1.5-2.4L25.6.1a2 2 0 0 1 2.4 1.5l.8 3.3z" fill="var(--txt)"/><path d="m8.2 3.9 4.6 5.6M15 2.3l4.6 5.6M21.8.8l4.6 5.6" stroke="var(--bg)" stroke-width="2.4"/><circle cx="16" cy="20.5" r="3.4" fill="var(--acc-t)"/></symbol>
<symbol id="i-live" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4" fill="currentColor"/></symbol>
<symbol id="i-cam" viewBox="0 0 24 24"><path d="M14.5 4h-5L7.5 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3.5z"/><circle cx="12" cy="13" r="3.5"/></symbol>
<symbol id="i-sw" viewBox="0 0 24 24"><path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/></symbol>
<symbol id="i-osd" viewBox="0 0 24 24"><rect x="2" y="4" width="20" height="16" rx="2"/><path d="M6 9h5M6 13h8M6 17h3M16 9h2"/></symbol>
<symbol id="i-sys" viewBox="0 0 24 24"><rect x="5" y="5" width="14" height="14" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"/></symbol>
<symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></symbol>
<symbol id="i-moon" viewBox="0 0 24 24"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></symbol>
<symbol id="i-batt" viewBox="0 0 24 24"><rect x="2" y="7" width="17" height="10" rx="2"/><path d="M22 11v2"/></symbol>
<symbol id="i-plane" viewBox="0 0 24 24"><circle cx="5" cy="5" r="2.5"/><circle cx="19" cy="5" r="2.5"/><circle cx="5" cy="19" r="2.5"/><circle cx="19" cy="19" r="2.5"/><path d="m7 7 3 3M17 7l-3 3M7 17l3-3M17 17l-3-3"/><rect x="9.5" y="9.5" width="5" height="5" rx="1"/></symbol>
<symbol id="i-wifi" viewBox="0 0 24 24"><path d="M2 8.8a15 15 0 0 1 20 0M5 12.6a10 10 0 0 1 14 0M8.5 16.4a5 5 0 0 1 7 0"/><circle cx="12" cy="20" r=".8" fill="currentColor"/></symbol>
<symbol id="i-scan" viewBox="0 0 24 24"><path d="M21 12a9 9 0 1 1-2.6-6.4M21 3v6h-6"/></symbol>
<symbol id="i-alert" viewBox="0 0 24 24"><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0zM12 9v4"/><circle cx="12" cy="17" r=".8" fill="currentColor"/></symbol>
<symbol id="i-lock" viewBox="0 0 24 24"><rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></symbol>
<symbol id="i-up" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12"/></symbol>
<symbol id="i-eye" viewBox="0 0 24 24"><path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></symbol>
<symbol id="i-pwr" viewBox="0 0 24 24"><path d="M12 2v10M18.4 6.6a9 9 0 1 1-12.8 0"/></symbol>
<symbol id="i-term" viewBox="0 0 24 24"><path d="m4 17 6-5-6-5M12 19h8"/></symbol>
</defs></svg>

<div class="app">
<nav class="nav" aria-label="Sections">
  <div class="brand logo"><svg><use href="#i-logo"/></svg>OpenSlate</div>
  <button data-v="live"><svg class="ic"><use href="#i-live"/></svg>Live</button>
  <button data-v="cam"><svg class="ic"><use href="#i-cam"/></svg>Camera</button>
  <button data-v="sw"><svg class="ic"><use href="#i-sw"/></svg>Switch</button>
  <button data-v="osd"><svg class="ic"><use href="#i-osd"/></svg>OSD</button>
  <button data-v="sys"><svg class="ic"><use href="#i-sys"/></svg>System</button>
  <div class="foot">OpenSlate by FPVRookie<br><span id="navFoot">GPL-3.0</span></div>
</nav>

<div class="main"><div class="wrap">
<header class="top">
  <div class="logo"><svg><use href="#i-logo"/></svg></div>
  <h1 id="title">Live</h1>
  <span class="pill" id="pill"><i></i><span id="pillTxt">Connecting</span></span>
  <button class="ibtn" id="themeBtn" aria-label="Toggle theme"><svg class="ic"><use id="themeUse" href="#i-moon"/></svg></button>
</header>

<div class="banner err" id="bnLost" hidden><svg class="ic"><use href="#i-alert"/></svg><div><b>Connection lost.</b> Retrying&hellip; make sure you are still on the OpenSlate Wi-Fi.</div></div>
<div class="banner" id="bnArmed" hidden><svg class="ic"><use href="#i-lock"/></svg><div><b>FC armed.</b> Pairing, scanning, Wi-Fi changes and reboot are locked until you disarm.</div></div>

<!-- ============================ LIVE ============================ -->
<section class="view" id="v-live">
  <div class="card hero down" id="hero">
    <div>
      <div class="state"><span class="tag" id="heroTag">No camera</span><span class="hint" id="heroCam"></span></div>
      <div class="timer" id="timer">--:--</div>
      <p class="info" id="heroInfo">Waiting for camera&hellip;</p>
    </div>
    <div>
      <button class="recbtn" id="recBtn" disabled aria-label="Start recording"><span></span></button>
      <div class="reclbl" id="recLbl">Start</div>
    </div>
  </div>

  <div class="grid g4">
    <div class="stat"><div class="k"><svg class="ic"><use href="#i-batt"/></svg>Camera battery</div>
      <div class="v" id="kBatt">--</div><div class="meter"><i id="kBattBar"></i></div><div class="h" id="kBattH">&nbsp;</div></div>
    <div class="stat"><div class="k"><svg class="ic"><use href="#i-sw"/></svg>Record switch</div>
      <div class="v" id="kRc">--</div><div class="h" id="kRcH">&nbsp;</div></div>
    <div class="stat"><div class="k"><svg class="ic"><use href="#i-plane"/></svg>Flight controller</div>
      <div class="v" id="kFc">--</div><div class="h" id="kFcH">&nbsp;</div></div>
    <div class="stat"><div class="k"><svg class="ic"><use href="#i-wifi"/></svg>OpenSlate</div>
      <div class="v" id="kSys">--</div><div class="h" id="kSysH">&nbsp;</div></div>
  </div>

  <div class="card">
    <header><h2>OSD preview</h2><button class="btn sm ghost" data-go="osd">Configure</button></header>
    <div class="osd" id="osdLive"></div>
  </div>
</section>

<!-- ============================ CAMERA ============================ -->
<section class="view" id="v-cam" hidden>
  <div class="card">
    <header><h2>Active camera</h2><span class="tag" id="camTag">Off</span></header>
    <div id="camActive"></div>
  </div>

  <div class="card">
    <header><h2>Saved cameras</h2><span class="meta num" id="camCount">0 / 4</span></header>
    <div class="list" id="camList"></div>
  </div>

  <div class="card">
    <header><h2>Add a camera</h2></header>
    <p class="sub">Power the camera on and hold it close. Nothing is saved until you tap <b>Pair</b>.</p>
    <div class="acts" style="margin-top:0">
      <div class="seg" id="brandSeg" role="group" aria-label="Camera brand">
        <button data-b="0" aria-pressed="false">DJI Osmo</button><button data-b="1" aria-pressed="false">GoPro</button>
      </div>
      <button class="btn pri" id="scanBtn"><svg class="ic"><use href="#i-scan"/></svg><span id="scanTxt">Scan</span></button>
    </div>
    <div class="prog" id="scanProg" hidden><i id="scanBar"></i></div>
    <div class="row" style="border:0;padding:16px 0 4px">
      <div class="t"><b>Show all nearby devices</b><p>Use this if your camera doesn&rsquo;t show up. The strongest signal is usually yours.</p></div>
      <label class="tg"><input type="checkbox" id="swAll"><i></i></label>
    </div>
    <div class="list" id="discList" style="margin-top:12px"></div>
    <details><summary>How pairing works</summary>
      <ol class="steps">
        <li>Pick the brand and tap Scan.</li>
        <li>Switch the camera on within about 1&nbsp;m.</li>
        <li>Tap Pair next to your camera.</li>
        <li>GoPro asks once for approval on its screen; DJI may too. After that it reconnects on its own.</li>
      </ol>
    </details>
  </div>
</section>

<!-- ============================ SWITCH ============================ -->
<section class="view" id="v-sw" hidden>
  <div class="cols">
  <div class="card">
    <header><h2>Record switch</h2></header>
    <div class="live">
      <div><div class="hint">Live value</div><div class="v" id="rcLive">----<small> &micro;s</small></div></div>
      <div class="sp"></div><span class="tag" id="rcTag">No RC</span>
    </div>
    <div class="field"><label for="selCh">Channel</label><select id="selCh"></select></div>
    <div class="field">
      <div class="lab"><span>ON threshold</span><output id="oThr">1500 &micro;s</output></div>
      <div class="rng"><input type="range" id="rngThr" min="1200" max="1800" step="25"><span class="mk" id="rcMk" hidden></span></div>
      <div class="scale"><span>1200</span><span>1500</span><span>1800</span></div>
      <p class="hint">The orange mark is your switch right now. Above the threshold counts as ON.</p>
    </div>
    <div class="field">
      <div class="lab"><span>Debounce</span><output id="oDeb">300 ms</output></div>
      <div class="rng"><input type="range" id="rngDeb" min="50" max="1000" step="50"></div>
      <p class="hint">How long the switch must hold a position before it counts.</p>
    </div>
  </div>

  <div class="grid" style="grid-template-columns:1fr;gap:16px">
  <div class="card">
    <header><h2>Automation</h2></header>
    <div class="row">
      <div class="t"><b>Record on arm</b><p>Start recording when Betaflight arms.</p></div>
      <label class="tg"><input type="checkbox" id="swRoa"><i></i></label>
    </div>
    <div class="row" id="rowSod">
      <div class="t"><b>Stop on disarm</b><p>Stop when the FC disarms. Otherwise keep recording until the switch goes off.</p></div>
      <label class="tg"><input type="checkbox" id="swSod"><i></i></label>
    </div>
    <p class="hint" style="margin-top:10px">The switch always wins: flipping it off ends an arm-triggered recording.</p>
  </div>

  <div class="card">
    <header><h2>Wi-Fi hotspot</h2><span class="tag ok" id="apTag">On</span></header>
    <div class="row" style="padding-top:0;margin-bottom:14px;border-bottom:1px solid var(--line)">
      <div class="t"><b>Off while armed</b><p>Wi-Fi turns off when the FC arms and back on when it disarms. Frees the radio for the camera links in flight.</p></div>
      <label class="tg"><input type="checkbox" id="swApArm"><i></i></label>
    </div>
    <div class="field"><label for="selWifi">Hotspot power switch</label><select id="selWifi"></select>
      <p class="hint">Flip low in flight to turn Wi-Fi off and save power &mdash; the camera link keeps running. The hotspot always starts on at boot.</p></div>
    <form id="wifiForm" autocomplete="off">
      <div class="field"><label for="inSsid">Network name</label><input type="text" id="inSsid" maxlength="32" placeholder="OpenSlate" data-lock></div>
      <div class="field"><label for="inPass">Password</label>
        <div class="pw"><input type="password" id="inPass" maxlength="63" placeholder="8&ndash;63 characters" data-lock><button type="button" id="pwEye" aria-label="Show password"><svg class="ic"><use href="#i-eye"/></svg></button></div></div>
      <div class="acts"><button class="btn pri" type="submit" data-lock>Save &amp; restart hotspot</button></div>
    </form>
  </div>
  </div>
  </div>
</section>

<!-- ============================ OSD ============================ -->
<section class="view" id="v-osd" hidden>
  <div class="cols">
  <div class="card">
    <header><h2>Live preview</h2></header>
    <div class="osd" id="osdPrev"></div>
    <p class="hint" style="margin-top:12px">Position each element in Betaflight Configurator &rarr; OSD (Custom Message 1&ndash;4). OpenSlate only supplies the text, up to 16 characters.</p>
  </div>
  <div class="card">
    <header><h2>Custom messages</h2></header>
    <div id="slots"></div>
  </div>
  </div>
</section>

<!-- ============================ SYSTEM ============================ -->
<section class="view" id="v-sys" hidden>
  <div class="card">
    <header><h2>Flight controller</h2><span class="tag" id="fcTag">No link</span></header>
    <div class="grid g3">
      <div class="stat"><div class="k">Firmware</div><div class="v" id="fFw">--</div><div class="h" id="fBoard">&nbsp;</div></div>
      <div class="stat"><div class="k">MSP API</div><div class="v" id="fApi">--</div><div class="h">protocol version</div></div>
      <div class="stat"><div class="k">Battery</div><div class="v" id="fVbat">--</div><div class="h" id="fLoop">&nbsp;</div></div>
    </div>
  </div>

  <div class="card">
    <header><h2>OpenSlate</h2><span class="meta mono" id="sVer"></span></header>
    <div class="grid g4">
      <div class="stat"><div class="k">CPU</div><div class="v" id="sCpu">--</div><div class="h" id="sCpuH">&nbsp;</div></div>
      <div class="stat"><div class="k">Free memory</div><div class="v" id="sHeap">--</div><div class="h">heap</div></div>
      <div class="stat"><div class="k">Uptime</div><div class="v" id="sUp">--</div><div class="h">&nbsp;</div></div>
      <div class="stat"><div class="k">Wi-Fi clients</div><div class="v" id="sSta">--</div><div class="h mono" id="sIp">&nbsp;</div></div>
    </div>
  </div>

  <div class="cols">
  <div class="card">
    <header><h2>MSP console</h2><span class="tag">Read-only</span></header>
    <div class="acts" style="margin-top:0;flex-wrap:nowrap">
      <select id="mspCmd" style="flex:1;min-width:0">
        <option value="1">1 &middot; API version</option><option value="2">2 &middot; FC variant</option>
        <option value="3">3 &middot; FC version</option><option value="4">4 &middot; Board info</option>
        <option value="5">5 &middot; Build info</option><option value="101" selected>101 &middot; Status</option>
        <option value="104">104 &middot; Motors</option><option value="105">105 &middot; RC channels</option>
        <option value="109">109 &middot; Altitude</option><option value="110">110 &middot; Analog</option>
        <option value="116">116 &middot; Box names</option><option value="119">119 &middot; Box IDs</option>
        <option value="130">130 &middot; Battery state</option>
      </select>
      <button class="btn" id="mspBtn"><svg class="ic"><use href="#i-term"/></svg>Send</button>
    </div>
    <pre id="mspOut">Pick a command and press Send.</pre>
  </div>

  <div class="card">
    <header><h2>Firmware update</h2></header>
    <label class="drop" id="drop"><input type="file" id="otaFile" accept=".bin">
      <svg class="ic"><use href="#i-up"/></svg><b id="dropTxt">Choose firmware .bin</b><span id="dropSub">or drop the file here</span></label>
    <div class="prog" id="otaProg" hidden><i id="otaBar"></i></div>
    <div class="acts"><button class="btn pri" id="otaBtn" disabled data-lock>Install update</button><span class="hint" id="otaMsg"></span></div>
    <p class="hint" style="margin-top:12px">Keep this page open and the board powered until it reboots. A failed update may need a USB re-flash.</p>
  </div>
  </div>

  <div class="card">
    <header><h2>Restart</h2></header>
    <div class="acts" style="margin-top:0"><button class="btn dng" id="rebootBtn" data-lock><svg class="ic"><use href="#i-pwr"/></svg>Reboot OpenSlate</button>
      <span class="hint">The hotspot drops for about 10&nbsp;s.</span></div>
  </div>
</section>

<footer class="ft">OpenSlate by FPVRookie<br><span id="ft">GPL-3.0</span></footer>
</div></div>
</div>

<div id="toast" role="status" aria-live="polite"><i></i><span id="toastTxt"></span></div>
<dialog id="dlg"><form method="dialog"><h3 id="dlgT"></h3><p id="dlgP"></p>
  <div class="acts"><button class="btn" value="no">Cancel</button><button class="btn pri" id="dlgOk" value="ok">OK</button></div></form></dialog>

<script>
'use strict';
const $=id=>document.getElementById(id), $$=s=>document.querySelectorAll(s);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const mmss=s=>{s=Math.max(0,s|0);const h=s/3600|0,m=(s/60|0)%60,x=s%60,p=n=>String(n).padStart(2,'0');return (h?h+':'+p(m):p(m))+':'+p(x)};
const dur=s=>{s|=0;const d=s/86400|0,h=(s/3600|0)%24,m=(s/60|0)%60;return d?d+'d '+h+'h':h?h+'h '+m+'m':m?m+'m '+(s%60)+'s':s+'s'};
const ch=i=>i<4?'CH'+(i+1):'AUX'+(i-3)+' (CH'+(i+1)+')';
const BRAND=['DJI Osmo','GoPro'];
const SLOT_NAMES=['Off','Camera status','Recording time','Camera battery','Link state','FC battery','Arm state','Camera count'];
const SCAN_MS=5000, COOLDOWN_MS=15000;

let S=null, armed=false, fails=0, pollT=0, inflight=false, again=false;
let brand=-1, scanUntil=0, scanEnd=0, scanTick=0, disc=[], lastErr='';
const synced={};

/* ---------- helpers ---------- */
function setHTML(el,h){if(el._h!==h){el.innerHTML=h;el._h=h;}}
function txt(id,v){const e=$(id);if(e.textContent!==v)e.textContent=v;}
let toastT=0;
function toast(m,k){const t=$('toast');$('toastTxt').textContent=m;t.className='show '+(k||'');clearTimeout(toastT);toastT=setTimeout(()=>t.className='',k==='err'?5000:2800);}
function ask(title,body,okLbl,danger){
  const d=$('dlg');
  if(!d.showModal)return Promise.resolve(confirm(title+'\n\n'+body));
  $('dlgT').textContent=title;$('dlgP').textContent=body;
  const ok=$('dlgOk');ok.textContent=okLbl||'OK';ok.className='btn '+(danger?'dng':'pri');
  d.returnValue='';d.showModal();
  return new Promise(r=>d.addEventListener('close',()=>r(d.returnValue==='ok'),{once:true}));
}
async function api(url,body){
  try{
    const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
    const t=await r.text();let j={};try{j=JSON.parse(t)}catch(_){}
    if(!r.ok)return{ok:false,error:j.error||('HTTP '+r.status)};
    return j;
  }catch(e){return{ok:false,error:'No connection to OpenSlate'}}
}
async function busy(btn,fn){btn.classList.add('busy');try{return await fn()}finally{btn.classList.remove('busy')}}
/* Write a form value from the server only when the server value changed and
   the user is not interacting with the control, so polls never clobber edits. */
function sync(el,v){
  if(document.activeElement===el||el._drag)return;
  if(synced[el.id]===v)return;synced[el.id]=v;
  if(el.type==='checkbox')el.checked=!!v;else el.value=v;
  if(el.type==='range')paint(el);
}
async function save(body,okMsg,els){
  const j=await api('/api/settings',body);
  if(j.ok){toast(okMsg,'ok');}else{toast(j.error||'Save failed','err');}
  (els||[]).forEach(e=>delete synced[e.id]);   // re-sync from server either way
  refresh();return j.ok;
}

/* ---------- theme ---------- */
function curTheme(){return document.documentElement.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light')}
function applyTheme(t){if(t)document.documentElement.dataset.theme=t;
  $('themeUse').setAttribute('href',curTheme()==='dark'?'#i-sun':'#i-moon');
  document.querySelector('meta[name=theme-color]').content=curTheme()==='dark'?'#0c0e12':'#f3f4f7';}
try{applyTheme(localStorage.getItem('os-theme'))}catch(_){applyTheme()}
$('themeBtn').onclick=()=>{const t=curTheme()==='dark'?'light':'dark';applyTheme(t);try{localStorage.setItem('os-theme',t)}catch(_){}};

/* ---------- navigation ---------- */
const TITLES={live:'Live',cam:'Camera',sw:'Switch',osd:'OSD',sys:'System'};
function go(v){
  if(!TITLES[v])v='live';
  $$('.view').forEach(s=>s.hidden=s.id!=='v-'+v);
  $$('.nav button').forEach(b=>{if(b.dataset.v===v)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current')});
  $('title').textContent=TITLES[v];
  if(location.hash!=='#'+v)history.replaceState(null,'','#'+v);
  scrollTo(0,0);
}
$$('.nav button').forEach(b=>b.onclick=()=>go(b.dataset.v));
document.addEventListener('click',e=>{const g=e.target.closest('[data-go]');if(g)go(g.dataset.go)});
addEventListener('hashchange',()=>go(location.hash.slice(1)));
go(location.hash.slice(1));

/* ---------- static option lists ---------- */
(()=>{
  const o=(v,t)=>'<option value="'+v+'">'+t+'</option>';
  let a='',w=o(-1,'Off &middot; hotspot always on');
  for(let i=0;i<16;i++){a+=o(i,ch(i));w+=o(i,ch(i));}
  $('selCh').innerHTML=a;$('selWifi').innerHTML=w;
  let s='';for(let i=0;i<4;i++){s+='<div class="slot"><b>CM'+(i+1)+'</b><select id="sl'+i+'" aria-label="Custom message '+(i+1)+'">'+
    SLOT_NAMES.map((n,v)=>o(v,n)).join('')+'</select><div class="now" id="now'+i+'"></div></div>';}
  $('slots').innerHTML=s;
})();

/* ---------- switch settings ---------- */
function paint(r){r.style.setProperty('--p',((r.value-r.min)/(r.max-r.min)*100)+'%')}
function rngLabels(){$('oThr').textContent=$('rngThr').value+' µs';$('oDeb').textContent=$('rngDeb').value+' ms';}
['rngThr','rngDeb'].forEach(id=>{const r=$(id);
  r.addEventListener('pointerdown',()=>r._drag=true);
  r.addEventListener('input',()=>{paint(r);rngLabels();});
  r.addEventListener('change',()=>{r._drag=false;
    save(id==='rngThr'?{threshold:+r.value}:{debounce:+r.value},id==='rngThr'?'Threshold saved':'Debounce saved',[r]);});
});
$('selCh').onchange=e=>save({auxChannel:+e.target.value},'Record channel: '+ch(+e.target.value),[e.target]);
$('swRoa').onchange=e=>{$('rowSod').classList.toggle('off',!e.target.checked);$('swSod').disabled=!e.target.checked;
  save({recordOnArm:e.target.checked},e.target.checked?'Record on arm enabled':'Record on arm disabled',[e.target]);};
$('swSod').onchange=e=>save({stopOnDisarm:e.target.checked},e.target.checked?'Stop on disarm enabled':'Stop on disarm disabled',[e.target]);
$('swApArm').onchange=e=>save({apOffArmed:e.target.checked},e.target.checked?'Hotspot turns off while armed':'Hotspot stays on while armed',[e.target]);
$('selWifi').onchange=e=>save({wifiSwitch:+e.target.value},+e.target.value<0?'Hotspot switch off':'Hotspot switch: '+ch(+e.target.value),[e.target]);
$('pwEye').onclick=()=>{const p=$('inPass');p.type=p.type==='password'?'text':'password'};
$('wifiForm').onsubmit=async e=>{
  e.preventDefault();
  const ssid=$('inSsid').value.trim(),pass=$('inPass').value.trim();
  if(!ssid)return toast('Enter a network name','err');
  if(pass.length<8||pass.length>63)return toast('Password needs 8–63 characters','err');
  if(!await ask('Restart hotspot?','Your device will disconnect. Join "'+ssid+'" with the new password to continue.','Save & restart'))return;
  const j=await api('/api/settings',{ssid,pass});
  if(j.ok){toast('Saved — reconnect to "'+ssid+'"','ok');$('inPass').value='';}else toast(j.error||'Save failed','err');
};
for(let i=0;i<4;i++)$('sl'+i).onchange=e=>save({['slot'+i]:+e.target.value},'CM'+(i+1)+': '+SLOT_NAMES[+e.target.value],[e.target]);

/* ---------- recording ---------- */
$('recBtn').onclick=async()=>{
  const on=!!(S&&S.rec&&S.rec.desired);
  const j=await api('/api/command',{cmd:on?'stop':'start'});
  if(!j.ok)toast(j.error||'Command failed','err');
  refresh();
};

/* ---------- cameras ---------- */
const brandAv=t=>'<div class="av '+(t?'gp':'dji')+'">'+(t?'GoPro':'DJI')+'</div>';
function bars(r){const n=r>=-55?4:r>=-67?3:r>=-80?2:1;return '<span class="bars b'+n+'" title="'+r+' dBm"><i></i><i></i><i></i><i></i></span>'}
function camState(st){
  return({CONNECTED:['Connected','ok'],SCANNING:['Searching','warn'],CONNECTING:['Connecting','warn'],PAIRING:['Approve on camera','warn']})[st]||['Off',''];
}
function renderCams(){
  const c=S.cam||{},list=S.cams||[],act=list.find(x=>x.a),st=camState(c.stateName);
  const t=$('camTag');t.textContent=act?st[0]:'None';t.className='tag '+(act?st[1]:'');
  setHTML($('camActive'),act?
    '<div class="cam">'+brandAv(act.t)+
    '<div class="t"><b>'+esc(act.n||act.m)+'</b><span>'+esc(act.m)+' &middot; '+BRAND[act.t?1:0]+'</span></div>'+
    '<button class="btn dng sm" data-forget="'+list.indexOf(act)+'"'+(armed?' disabled':'')+'>Forget</button></div>'
    :'<div class="empty">No camera selected. Pair one below.</div>');
  $('camCount').textContent=list.length+' / 4';
  const online=new Set((S.pending_cams||[]).map(p=>p.mac));
  setHTML($('camList'),list.length?list.map((x,i)=>{
    const tag=x.a?'<span class="tag acc">Active</span>':x.on?'<span class="tag ok">Ready</span>':online.has(x.m)?'<span class="tag">In range</span>':'';
    return '<div class="item'+(x.a?' active':'')+'">'+brandAv(x.t)+
      '<div class="t"><b>'+esc(x.n||x.m)+'</b><span>'+esc(x.m)+'</span></div><div class="x">'+tag+
      (x.a?'':'<button class="btn sm" data-sel="'+i+'"'+(armed?' disabled':'')+'>Use</button>')+
      '<button class="btn sm ghost" data-forget="'+i+'" aria-label="Forget"'+(armed?' disabled':'')+'>&times;</button></div></div>';
  }).join(''):'<div class="empty">No saved cameras yet.</div>');
}
function renderDisc(){
  const saved=new Set((S&&S.cams||[]).map(c=>c.m)),seen=new Set();
  const rows=disc.filter(r=>!saved.has(r.mac)&&!seen.has(r.mac)&&seen.add(r.mac));
  const scanning=Date.now()<scanEnd;
  setHTML($('discList'),rows.length?rows.map(r=>{const t=r.t==='GoPro'?1:0;
    return '<div class="item">'+brandAv(t)+'<div class="t"><b>'+esc(r.n||'Unnamed device')+'</b><span>'+esc(r.mac)+'</span></div>'+
      '<div class="x">'+bars(r.rssi)+'<button class="btn sm pri" data-pair="'+esc(r.mac)+'" data-t="'+t+'"'+(armed?' disabled':'')+'>Pair</button></div></div>';
  }).join(''):(scanning?'<div class="empty">Looking for cameras&hellip;</div>':disc._done?'<div class="empty">No new cameras found. Is it switched on and close by?</div>':''));
}
document.addEventListener('click',async e=>{
  const b=e.target.closest('[data-sel],[data-forget],[data-pair]');if(!b||b.disabled)return;
  if(b.dataset.sel!=null){
    const j=await busy(b,()=>api('/api/camera',{select:+b.dataset.sel}));
    toast(j.ok?'Switching camera…':j.error||'Failed',j.ok?'ok':'err');
  }else if(b.dataset.forget!=null){
    const c=(S.cams||[])[+b.dataset.forget]||{};
    if(!await ask('Forget '+(c.n||c.m||'camera')+'?','It will disconnect and must be paired again to use it.','Forget',true))return;
    const j=await api('/api/camera',{remove:+b.dataset.forget});
    toast(j.ok?'Camera forgotten':j.error||'Failed',j.ok?'ok':'err');
  }else{
    const j=await busy(b,()=>api('/api/camera',{pair:{mac:b.dataset.pair,type:+b.dataset.t}}));
    if(j.ok){toast('Paired — connecting. Approve on the camera if asked.','ok');disc=disc.filter(r=>r.mac!==b.dataset.pair);renderDisc();}
    else toast(j.error||'Pairing failed','err');
  }
  refresh();
});
$$('#brandSeg button').forEach(b=>b.onclick=()=>{brand=+b.dataset.b;syncBrand();});
function syncBrand(){$$('#brandSeg button').forEach(b=>{b.setAttribute('aria-pressed',+b.dataset.b===brand);b.disabled=armed;});}
$('swAll').onchange=e=>save({scanAll:e.target.checked},e.target.checked?'Showing all nearby devices':'Showing cameras only',[e.target]);
async function pollScan(){
  try{const r=await fetch('/api/scan',{cache:'no-store'});if(r.ok){const d=await r.json();disc=d.results||[];renderDisc();}}catch(_){}
}
function scanUI(){
  const now=Date.now(),btn=$('scanBtn'),scanning=now<scanEnd,cool=now<scanUntil;
  btn.disabled=armed||cool||brand<0;btn.classList.toggle('busy',scanning);
  $('scanTxt').textContent=scanning?'Scanning…':cool?'Wait '+Math.ceil((scanUntil-now)/1000)+'s':'Scan';
  $('scanProg').hidden=!scanning;$('scanBar').style.width=scanning?Math.min(100,(1-(scanEnd-now)/SCAN_MS)*100)+'%':'0';
  if(!cool&&scanTick){clearInterval(scanTick);scanTick=0;}
}
$('scanBtn').onclick=async()=>{
  if(brand<0)return toast('Pick DJI or GoPro first','err');
  const btn=$('scanBtn');
  if(S&&S.cam&&S.cam.type!==brand){const j=await api('/api/settings',{camera:brand});if(!j.ok)return toast(j.error||'Failed','err');}
  const j=await busy(btn,()=>api('/api/camera',{scan:true}));
  if(!j.ok)return toast(j.error||'Scan failed','err');
  disc=[];const t=Date.now();scanEnd=t+SCAN_MS;scanUntil=t+COOLDOWN_MS;renderDisc();
  let n=0;const pt=setInterval(()=>{pollScan();if(++n>=5){clearInterval(pt);disc._done=true;renderDisc();}},1500);
  pollScan();
  if(!scanTick)scanTick=setInterval(()=>{scanUI();if(Date.now()>=scanEnd)renderDisc();},250);
  scanUI();
};

/* ---------- system ---------- */
$('rebootBtn').onclick=async()=>{
  if(!await ask('Reboot OpenSlate?','Wi-Fi and the camera link drop for about 10 seconds.','Reboot',true))return;
  const j=await api('/api/command',{cmd:'reboot'});
  toast(j.ok?'Rebooting…':j.error||'Failed',j.ok?'ok':'err');
};
$('mspBtn').onclick=()=>busy($('mspBtn'),async()=>{
  const out=$('mspOut');out.textContent='Querying FC…';
  const j=await api('/api/msp',{cmd:+$('mspCmd').value});
  if(!j.ok){out.textContent='Error: '+(j.error||'?');return;}
  const p=(j.payload||'').match(/../g)||[];let s='MSP '+j.cmd+' · '+j.len+' bytes\n';
  for(let i=0;i<p.length;i+=16){
    const row=p.slice(i,i+16),asc=row.map(h=>{const c=parseInt(h,16);return c>31&&c<127?String.fromCharCode(c):'.'}).join('');
    s+='\n'+i.toString(16).padStart(4,'0')+'  '+row.join(' ').padEnd(47)+'  '+asc;}
  out.textContent=s;out.style.color='var(--txt)';
});
const drop=$('drop'),ota=$('otaFile');
function pickFile(f){
  if(f&&!/\.bin$/i.test(f.name)){toast('Choose a .bin firmware file','err');f=null;}
  drop._f=f||null;$('dropTxt').textContent=f?f.name:'Choose firmware .bin';
  $('dropSub').textContent=f?(f.size/1024).toFixed(0)+' KB · tap to change':'or drop the file here';$('otaBtn').disabled=!f||armed;
}
ota.onchange=()=>pickFile(ota.files[0]);
['dragenter','dragover'].forEach(n=>drop.addEventListener(n,e=>{e.preventDefault();drop.classList.add('over')}));
['dragleave','drop'].forEach(n=>drop.addEventListener(n,e=>{e.preventDefault();drop.classList.remove('over')}));
drop.addEventListener('drop',e=>pickFile(e.dataTransfer.files[0]));
$('otaBtn').onclick=async()=>{
  const f=drop._f;if(!f)return;
  if(!await ask('Install '+f.name+'?','OpenSlate reboots when done. Keep this page open and the board powered.','Install'))return;
  const fd=new FormData();fd.append('firmware',f);
  const x=new XMLHttpRequest(),msg=$('otaMsg'),bar=$('otaBar');
  $('otaProg').hidden=false;$('otaBtn').disabled=true;
  x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);bar.style.width=p+'%';msg.textContent='Uploading '+p+'%';}};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(_){}
    if(x.status===200&&j.ok){msg.textContent='Done — rebooting…';toast('Update installed, rebooting','ok');setTimeout(()=>location.reload(),9000);}
    else{msg.textContent='Failed';toast(j.error||'Update failed (HTTP '+x.status+')','err');$('otaBtn').disabled=false;}};
  x.onerror=()=>{msg.textContent='Upload interrupted';toast('Upload interrupted','err');$('otaBtn').disabled=false;};
  x.open('POST','/api/ota');x.send(fd);
};

/* ---------- render ---------- */
function osdHTML(osd,slots){
  let h='';for(let i=0;i<4;i++){const off=!slots[i];
    h+='<div'+(off?' class="off"':'')+'><b>CM'+(i+1)+'</b><span>'+esc(off?'—':(osd[i]||' '))+'</span></div>';}
  return h;
}
function render(){
  const c=S.cam||{},f=S.fc||{},r=S.rec||{},y=S.sys||{};
  const st=c.stateName||'OFF',ready=st==='CONNECTED',rec=!!r.desired;
  armed=!!(f.alive&&f.armed);
  $('bnArmed').hidden=!armed;
  $$('[data-lock]').forEach(e=>{if(e.id!=='otaBtn')e.disabled=armed});
  $('otaBtn').disabled=armed||!drop._f;

  /* header pill */
  const pill=$('pill'),cs=camState(st);
  pill.className='pill '+(rec&&ready?'rec':cs[1]);
  txt('pillTxt',rec&&ready?'REC '+mmss(c.recTime):ready?'Camera ready':cs[0]==='Off'?'No camera':cs[0]);

  /* hero */
  const hero=$('hero');hero.className='card hero'+(ready?(rec?' recording':''):' down');
  const tag=$('heroTag');
  tag.textContent=ready?(rec?'Recording':'Standby'):cs[0]==='Off'?'No camera':cs[0];
  tag.className='tag '+(ready?(rec?'rec':'ok'):cs[1]||'');
  txt('heroCam',ready?(c.model||c.name||''):'');
  txt('timer',ready?mmss(c.recTime):'--:--');
  const why={SCANNING:'Searching for your camera. Switch it on and keep it nearby.',CONNECTING:'Connecting to the camera…',
    PAIRING:'Approve the pairing request on the camera screen. This is only needed once.',
    OFF:(S.cams||[]).length?'Camera link is down. Retrying automatically.':'No camera paired yet. Open the Camera tab to add one.'};
  txt('heroInfo',ready?(rec?'Tap the button or flip your switch to stop.':'Arm or flip your record switch, or tap the button.')+
    (S.connectedCams>1?' '+S.connectedCams+' cameras connected.':''):why[st]||why.OFF);
  const rb=$('recBtn');rb.disabled=!ready;rb.classList.toggle('on',ready&&rec);
  rb.setAttribute('aria-label',rec?'Stop recording':'Start recording');txt('recLbl',rec?'Stop':'Start');

  /* tiles */
  const b=c.batt;txt('kBatt',b!=null&&b>=0?b+'%':'--');
  const bb=$('kBattBar');bb.style.width=(b>0?b:0)+'%';bb.style.background=b<20?'var(--rec)':b<40?'var(--warn)':'var(--ok)';
  const bh=[];if(c.battMv>0)bh.push((c.battMv/1000).toFixed(2)+' V');if(c.battTemp!=null&&c.battTemp>-9999)bh.push((c.battTemp/10).toFixed(1)+' °C');
  txt('kBattH',bh.length?bh.join(' · '):(ready&&!(b>=0)?'Not reported yet':' '));
  txt('kRc',r.rcValue>0?(r.switchOn?'ON':'OFF'):'No RC');
  txt('kRcH',ch(r.auxCh|0)+(r.rcValue>0?' · '+r.rcValue+' µs':''));
  txt('kFc',!f.alive?'No link':f.armed?'Armed':'Disarmed');
  txt('kFcH',f.alive?(f.vbat10/10).toFixed(1)+' V'+(f.fw?' · BF '+f.fw:''):'Check the UART wiring');
  txt('kSys',y.cpu!=null?y.cpu+'% CPU':'--');
  txt('kSysH',(y.sta!=null?y.sta+(y.sta===1?' client':' clients'):'')+' · up '+dur(y.uptime||0));

  /* OSD */
  const osd=S.osd||[],slots=S.slots||[0,0,0,0];
  setHTML($('osdLive'),osdHTML(osd,slots));setHTML($('osdPrev'),osdHTML(osd,slots));
  for(let i=0;i<4;i++){sync($('sl'+i),slots[i]);txt('now'+i,slots[i]?'Now: '+(osd[i]||'(empty)'):'Hidden');}

  /* switch */
  sync($('selCh'),r.auxCh|0);sync($('rngThr'),r.thr||1500);sync($('rngDeb'),r.deb||300);rngLabels();
  sync($('swRoa'),!!r.roa);sync($('swSod'),!!r.sod);
  $('swSod').disabled=!$('swRoa').checked;$('rowSod').classList.toggle('off',!$('swRoa').checked);
  sync($('selWifi'),S.wifiSwitch!=null&&S.wifiSwitch>=0?S.wifiSwitch:-1);
  const live=r.rcValue>0;
  $('rcLive').innerHTML=(live?r.rcValue:'----')+'<small> µs</small>';
  const rt=$('rcTag');rt.textContent=live?(r.switchOn?'ON':'OFF'):'No RC';rt.className='tag '+(live?(r.switchOn?'rec':'ok'):'warn');
  const mk=$('rcMk');mk.hidden=!live;
  if(live){mk.style.left=Math.max(0,Math.min(100,(r.rcValue-1200)/6))+'%';mk.classList.toggle('on',!!r.switchOn);}
  const at=$('apTag');at.textContent=S.wifiOn===false?'Off':'On';at.className='tag '+(S.wifiOn===false?'':'ok');
  sync($('swAll'),!!S.scanAll);
  sync($('swApArm'),S.apOffArmed!==false);

  /* cameras */
  if(brand<0&&c.type!=null)brand=c.type;
  syncBrand();scanUI();renderCams();renderDisc();

  /* system */
  const ft=$('fcTag');ft.textContent=!f.alive?'No link':f.armed?'Armed':'Connected';ft.className='tag '+(!f.alive?'warn':f.armed?'rec':'ok');
  txt('fFw',f.fw||'--');txt('fBoard',f.board||' ');txt('fApi',f.api||'--');
  txt('fVbat',f.alive?(f.vbat10/10).toFixed(1)+' V':'--');
  txt('fLoop',f.cycle?'loop '+f.cycle+' µs · RSSI '+f.rssi+'%':' ');
  txt('sCpu',y.cpu!=null?y.cpu+'%':'--');txt('sCpuH',y.cpuLoop!=null?'app task '+y.cpuLoop+'%':' ');
  txt('sHeap',y.heap?Math.round(y.heap/1024)+' KB':'--');txt('sUp',dur(y.uptime||0));
  txt('sSta',y.sta!=null?String(y.sta):'--');txt('sIp',y.ip||' ');
  const ver=(y.version?y.version+' · ':'')+'GPL-3.0';txt('sVer',y.version||'');txt('navFoot',ver);txt('ft',ver+(y.ip?' · '+y.ip:''));

  /* backend connect errors: toast once per distinct message */
  if(S.lastError&&S.lastError!==lastErr)toast(S.lastError,'err');
  lastErr=S.lastError||'';
}

/* ---------- polling: one loop, never stacked ---------- */
async function poll(){
  if(inflight){again=true;return;}
  inflight=true;clearTimeout(pollT);
  try{
    const ctl=new AbortController(),to=setTimeout(()=>ctl.abort(),4000);
    const res=await fetch('/api/status',{cache:'no-store',signal:ctl.signal});clearTimeout(to);
    const j=await res.json();
    if(j&&j.cam){S=j;fails=0;render();}
  }catch(_){fails++;}
  $('bnLost').hidden=fails<3;
  inflight=false;
  if(again){again=false;return poll();}
  pollT=setTimeout(poll,document.hidden?5000:1500);
}
const refresh=()=>poll();
document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh()});
poll();
</script>
</body>
</html>)rawliteral";

#endif // WEB_ASSETS_H
