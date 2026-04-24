/*
 * drone_page.h — Page HTML V3.5 en PROGMEM
 *
 * V3.5 :
 *   - USB data remplacé par BLE (Web Bluetooth)
 *   - SSID/pwd drone.local, mDNS, DHCP maison
 *   - Tabs Info/Meteo/Drones mutuellement exclusifs
 *   - ADS-B cerclage conditionnel
 *   - Scan: DETECTION 1-14, CLASSIC 1,6,11, TRACKING canaux actifs
 *
 * V3.4 :
 *   - Fix ADS-B bouton invisible sur mobile (header flex-wrap)
 *
 * V3.3 :
 *   - Tag "myself" : auto (position GPS) + manuel (1er clic)
 *   - Icone jaune distincte + badge MY dans la liste
 */

#ifndef DRONE_PAGE_H
#define DRONE_PAGE_H

const char PAGE_HTML[] PROGMEM = R"rawpage(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover">
<title>AH &amp; EPERRET — Drone RX V3.5</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0e14;color:#c8d0da;height:100vh;height:100dvh;display:flex;flex-direction:column;overflow:hidden;padding-top:env(safe-area-inset-top);padding-bottom:env(safe-area-inset-bottom);padding-left:env(safe-area-inset-left);padding-right:env(safe-area-inset-right)}
#header{background:#111820;border-bottom:1px solid #1e2a38;padding:6px 12px;display:flex;align-items:center;justify-content:space-between;gap:6px;flex-shrink:0}
#header h1{font-size:12px;color:#4fc3f7;white-space:nowrap;letter-spacing:1px}
.hdr-right{display:flex;align-items:center;gap:6px}
.proto-counters{display:flex;gap:6px}
.pb{font-size:10px;padding:2px 5px;border-radius:3px;border:1px solid;font-weight:bold}
.pb-FR{color:#4fc3f7;border-color:#4fc3f7}.pb-ODID{color:#66bb6a;border-color:#66bb6a}
.pb-DJI{color:#ffa726;border-color:#ffa726}.pb-PAR{color:#ce93d8;border-color:#ce93d8}
.dot{width:8px;height:8px;border-radius:50%;background:#555;transition:background .3s}
.dot.ble{background:#4fc3f7;box-shadow:0 0 6px #4fc3f7}.dot.ws{background:#66bb6a;box-shadow:0 0 6px #66bb6a}
.dot.off{background:#ef5350;box-shadow:0 0 4px #ef5350}
#status{font-size:10px;color:#778}
button{background:#1a2533;color:#4fc3f7;border:1px solid #2a3a4d;padding:4px 10px;font-family:inherit;font-size:11px;cursor:pointer;border-radius:3px}
button:hover{background:#243344;border-color:#4fc3f7}
button.save{background:#2a1a33;color:#ce93d8;border-color:#4a2a5d}
button.save:hover{background:#3a2a44;border-color:#ce93d8}
#main{flex:1;display:flex;flex-direction:column;overflow:hidden}
#map-wrap{flex:1;min-height:0;position:relative}
#map{width:100%;height:100%}
#nomap{width:100%;height:100%;background:#0d1117;color:#445;font-size:14px;align-items:center;justify-content:center;text-align:center;display:none}
.alt-label{background:rgba(10,14,20,.85);border:1px solid #4fc3f7;color:#4fc3f7;padding:1px 5px;font-size:11px;font-family:'Courier New',monospace;border-radius:2px;white-space:nowrap}
#bottom{background:#0d1117;border-top:2px solid #1e2a38;height:200px;flex-shrink:0;display:flex;overflow:hidden}
@media(max-height:600px){#bottom{height:150px}}
@media(max-height:500px){#bottom{height:120px}}
#connpanel{width:210px;border-right:1px solid #1e2a38;padding:8px;display:flex;flex-direction:column;gap:3px;flex-shrink:0;overflow-y:auto}
#connpanel label{font-size:10px;color:#556;text-transform:uppercase;letter-spacing:1px}
.conn-info{font-size:10px;color:#445;margin:1px 0}
.conn-btns{display:flex;gap:4px;flex-wrap:wrap}
.conn-btns button{flex:1;min-width:70px}
.hint{font-size:9px;color:#666;margin:2px 0;line-height:1.3}
#decode{flex:1;padding:8px 12px;overflow-y:auto;display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:2px 16px;align-content:start}
.ds{grid-column:1/-1;font-size:10px;color:#556;text-transform:uppercase;letter-spacing:1px;padding:4px 0 2px;margin-top:4px;border-bottom:1px solid #1e2a38}
.tf{display:flex;align-items:baseline;gap:6px;padding:2px 0}
.tn{font-size:11px;color:#778;min-width:80px}
.tv{font-size:12px;font-weight:bold}
.tv.gps{color:#66bb6a}.tv.alt{color:#ffa726}.tv.spd{color:#ef5350}.tv.id{color:#ce93d8}.tv.pr{color:#4fc3f7}.tv.nfo{color:#c8d0da}
#drones{width:160px;border-left:1px solid #1e2a38;padding:8px;overflow-y:auto;flex-shrink:0}
#drones label{font-size:10px;color:#556;text-transform:uppercase;letter-spacing:1px}
.de{padding:4px 6px;margin-top:4px;border-radius:3px;cursor:pointer;font-size:11px;border:1px solid transparent}
.de:hover{border-color:#2a3a4d}.de.sel{background:#1a2533;border-color:#4fc3f7}
.de.myself{border-color:#ff0;background:#1a1a00}.de.myself.sel{border-color:#ff0}
.my-tag{font-size:8px;color:#ff0;margin-left:3px;font-weight:bold;vertical-align:middle}
.dc{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;vertical-align:middle}
.dp{font-size:9px;padding:1px 3px;border-radius:2px;margin-left:4px;vertical-align:middle}
::-webkit-scrollbar{width:4px}::-webkit-scrollbar-track{background:#0a0e14}::-webkit-scrollbar-thumb{background:#2a3a4d;border-radius:2px}
button.meteo{background:#1a2a1a;color:#66bb6a;border-color:#2a4a2d}
button.meteo:hover{background:#2a3a2a;border-color:#66bb6a}
#meteo{flex:1;padding:8px 12px;overflow-y:auto;display:none;font-size:11px;line-height:1.5}
#meteo .ms{font-size:10px;color:#556;text-transform:uppercase;letter-spacing:1px;padding:4px 0 2px;border-bottom:1px solid #1e2a38;margin-top:6px}
#meteo .ms:first-child{margin-top:0}
.gng{display:inline-block;padding:2px 8px;border-radius:3px;font-weight:bold;font-size:12px;margin:4px 0}
.gng-go{background:#1a3a1a;color:#66bb6a;border:1px solid #66bb6a}
.gng-mar{background:#3a3a1a;color:#ffa726;border:1px solid #ffa726}
.gng-no{background:#3a1a1a;color:#ef5350;border:1px solid #ef5350}
.wrow{display:flex;gap:8px;padding:2px 0}
.wh{color:#778;min-width:45px}.wv{color:#66bb6a;font-weight:bold}.wg{color:#ffa726}.wd{color:#4fc3f7}
.metar-raw{font-size:10px;color:#556;word-break:break-all;margin:2px 0;padding:3px;background:#0d1117;border-radius:2px}
/* ── Mobile : tabs en bas sur petit ecran ── */
#btabs{display:none;background:#111820;border-bottom:1px solid #1e2a38;flex-shrink:0}
#btabs button{flex:1;background:none;border:none;border-bottom:2px solid transparent;color:#556;padding:6px 4px;font-size:10px;font-family:inherit;text-transform:uppercase;letter-spacing:1px}
#btabs button.active{color:#4fc3f7;border-bottom-color:#4fc3f7}
@media(max-width:700px){
  #header{flex-wrap:wrap}
  #header h1{font-size:10px;letter-spacing:0}
  .proto-counters{gap:3px}
  .pb{font-size:9px;padding:1px 3px}
  .hdr-right{flex-wrap:wrap;gap:3px;justify-content:flex-end}
  #status{display:none}
  #bottom{flex-direction:column;height:auto;max-height:50vh}
  #btabs{display:flex}
  #connpanel{width:100%;border-right:none;border-bottom:1px solid #1e2a38;padding:6px}
  #decode{min-height:120px}
  #drones{width:100%;border-left:none;border-top:1px solid #1e2a38}
  #meteo{min-height:120px}
  .bpanel{display:none}
  .bpanel.active{display:block}
  #connpanel.active{display:flex}
  #decode.active{display:grid}
}
/* ── TX Config Modal ── */
#txmodal{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.85);z-index:9999;align-items:center;justify-content:center}
#txmodal.show{display:flex}
#txform{background:#111820;border:1px solid #2a3a4d;border-radius:6px;padding:16px;width:320px;max-width:95vw;max-height:90vh;overflow-y:auto}
#txform label{font-size:10px;color:#556;text-transform:uppercase;letter-spacing:1px;display:block;margin-top:8px}
#txform select,#txform input{width:100%;background:#0a0e14;color:#4fc3f7;border:1px solid #2a3a4d;padding:6px 8px;font-family:inherit;font-size:12px;border-radius:3px;margin-top:2px}
#txform select:focus,#txform input:focus{border-color:#4fc3f7;outline:none}
.txbtns{display:flex;gap:8px;margin-top:12px}
.txbtns button{flex:1;padding:8px;font-size:12px}
/* ── ADS-B Aircraft ── */
.ac-label{background:rgba(10,14,20,.85);border:1px solid #26c6da;color:#26c6da;padding:1px 4px;font-size:10px;font-family:'Courier New',monospace;border-radius:2px;white-space:nowrap}
</style>
</head>
<body>
<div id="header">
  <h1>AH &amp; EPERRET — DRONE RX</h1>
  <div class="proto-counters">
    <span class="pb pb-FR">FR:<span id="cFR">0</span></span>
    <span class="pb pb-ODID">ODID:<span id="cODID">0</span></span>
    <span class="pb pb-DJI">DJI:<span id="cDJI">0</span></span>
    <span class="pb pb-PAR">PAR:<span id="cPAR">0</span></span>
  </div>
  <div class="hdr-right">
    <div class="dot" id="dot"></div>
    <span id="status">Deconnecte</span>
    <button onclick="toggleSound()" id="btnSnd" title="Son">&#128264;</button>
    <button onclick="toggleADSB()" id="btnADSB" style="color:#26c6da">ADS-B</button>
    <button onclick="startSim()">Simu</button>
    <button onclick="stopAll()" id="btnStop" style="display:none">Stop</button>
  </div>
</div>
<div id="main">
  <div id="map-wrap">
    <div id="map"></div>
    <div id="nomap">Pas de connexion internet<br>Carte indisponible<br><br>Donnees drone ci-dessous</div>
  </div>
  <div id="bottom">
    <div id="btabs">
      <button class="active" onclick="showTab('connpanel')">Conn</button>
      <button onclick="showTab('decode')">Info</button>
      <button onclick="showTab('meteo')">Meteo</button>
      <button onclick="showTab('drones')">Drones</button>
    </div>
    <div id="connpanel" class="bpanel active">
      <label>Connexion</label>
      <div class="conn-btns">
        <button onclick="connectBLE()" id="btnBLE">BLE Connect</button>
        <button onclick="disconnBLE()" id="btnDBLE" style="display:none">Deco BLE</button>
      </div>
      <div class="conn-btns">
        <button onclick="connectWS()" id="btnWS">WiFi WS</button>
        <button onclick="disconnWS()" id="btnDWS" style="display:none">Deco WS</button>
      </div>
      <div class="conn-info" id="connInfo">Aucune connexion</div>
      <label style="margin-top:2px">WiFi Board</label>
      <div class="conn-info">SSID: <b style="color:#4fc3f7">drone.local</b></div>
      <div class="conn-info">Pass: <b style="color:#4fc3f7">drone.local</b></div>
      <div class="conn-info">WS: ws://drone.local:81</div>
      <div class="conn-btns" style="margin-top:3px">
        <button onclick="centerMe()">Centrer</button>
        <button class="save" onclick="savePage()">Sauver</button>
        <button class="meteo" onclick="toggleMeteoPanel()">Meteo</button>
        <button style="background:#2a2a1a;color:#ffa726;border-color:#4a3a1d" onclick="showTxConfig()">TX Config</button>
      </div>
    </div>
    <div id="decode" class="bpanel">
      <div class="ds">SOURCE</div>
      <div class="tf"><span class="tn">Proto</span><span class="tv pr" id="vP">-</span></div>
      <div class="tf"><span class="tn">MAC</span><span class="tv nfo" id="vMAC">-</span></div>
      <div class="tf"><span class="tn">RSSI</span><span class="tv nfo" id="vRSSI">-</span></div>
      <div class="tf"><span class="tn">Canal</span><span class="tv nfo" id="vCH">-</span></div>
      <div class="ds">IDENTIFICATION</div>
      <div class="tf"><span class="tn">ID</span><span class="tv id" id="vID">-</span></div>
      <div class="tf"><span class="tn">Type ID</span><span class="tv nfo" id="vIDT">-</span></div>
      <div class="tf"><span class="tn">Type drone</span><span class="tv nfo" id="vUAT">-</span></div>
      <div class="ds">POSITION</div>
      <div class="tf"><span class="tn">Lat</span><span class="tv gps" id="vLat">-</span></div>
      <div class="tf"><span class="tn">Lon</span><span class="tv gps" id="vLon">-</span></div>
      <div class="tf"><span class="tn">Alt MSL</span><span class="tv alt" id="vAlt">-</span></div>
      <div class="tf"><span class="tn">Haut AGL</span><span class="tv alt" id="vAGL">-</span></div>
      <div class="tf"><span class="tn">Home Lat</span><span class="tv gps" id="vHLat">-</span></div>
      <div class="tf"><span class="tn">Home Lon</span><span class="tv gps" id="vHLon">-</span></div>
      <div class="ds">DYNAMIQUE</div>
      <div class="tf"><span class="tn">Vitesse</span><span class="tv spd" id="vSpd">-</span></div>
      <div class="tf"><span class="tn">Route</span><span class="tv spd" id="vHdg">-</span></div>
      <div class="tf"><span class="tn">Vit vert</span><span class="tv spd" id="vVs">-</span></div>
      <div class="ds">BRUT</div>
      <div class="tf" style="grid-column:1/-1"><span class="tn">Hex</span><span class="tv nfo" id="vHex" style="font-size:10px;word-break:break-all;font-weight:normal">-</span></div>
    </div>
    <div id="meteo" class="bpanel">
      <div style="color:#445;font-size:11px">Chargement...</div>
    </div>
    <div id="drones" class="bpanel">
      <label>Drones</label>
      <div id="droneList"><div style="color:#445;font-size:11px;margin-top:8px">Aucun</div></div>
    </div>
  </div>
</div>
<script>
/* ═══════════════════════════════════════════════════════════
   SAUVEGARDE PAGE — telecharge le HTML complet
   ═══════════════════════════════════════════════════════════ */
function savePage(){
  var html=document.documentElement.outerHTML;
  var blob=new Blob(['<!DOCTYPE html>\n'+html],{type:'text/html'});
  var a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download='DroneRX_V3.html';
  a.click();
  URL.revokeObjectURL(a.href);
  document.getElementById('connInfo').textContent='Page sauvegardee !';
}

/* ═══════════════════════════════════════════════════════════
   CARTE
   ═══════════════════════════════════════════════════════════ */
var map=null,myMarker=null,myLat=5.16,myLon=-52.65;
var mapReady=false,firstDroneCentered=false;

function saveMapView(){
  if(!map)return;
  try{var c=map.getCenter();
    localStorage.setItem('drx_lat',c.lat);
    localStorage.setItem('drx_lon',c.lng);
    localStorage.setItem('drx_zoom',map.getZoom());
  }catch(e){}
}
function loadMapView(){
  try{var lat=parseFloat(localStorage.getItem('drx_lat'));
    var lon=parseFloat(localStorage.getItem('drx_lon'));
    var z=parseInt(localStorage.getItem('drx_zoom'));
    if(!isNaN(lat)&&!isNaN(lon)&&!isNaN(z)){myLat=lat;myLon=lon;return{lat:lat,lon:lon,z:z};}
  }catch(e){}
  return null;
}

function initMap(){
  try{
    var s=document.createElement('link');s.rel='stylesheet';
    s.href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css';
    document.head.appendChild(s);
    var j=document.createElement('script');
    j.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';
    j.onload=function(){
      document.getElementById('map').style.display='block';
      document.getElementById('nomap').style.display='none';
      var saved=loadMapView();
      var sLat=saved?saved.lat:myLat,sLon=saved?saved.lon:myLon,sZ=saved?saved.z:14;
      map=L.map('map',{zoomControl:false}).setView([sLat,sLon],sZ);
      L.control.zoom({position:'topright'}).addTo(map);
      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'}).addTo(map);
      myMarker=L.circleMarker([sLat,sLon],{radius:7,fillColor:'#fff',fillOpacity:.9,color:'#4fc3f7',weight:2}).addTo(map).bindTooltip('Moi');
      mapReady=true;
      map.on('moveend',saveMapView);map.on('zoomend',saveMapView);
      if(navigator.geolocation){
        navigator.geolocation.watchPosition(function(p){
          myLat=p.coords.latitude;myLon=p.coords.longitude;
          if(p.coords.altitude!==null)myAlt=p.coords.altitude;
          if(myMarker)myMarker.setLatLng([myLat,myLon]);
        },null,{enableHighAccuracy:true,maximumAge:5000});
      }
    };
    j.onerror=function(){document.getElementById('map').style.display='none';document.getElementById('nomap').style.display='flex';};
    document.head.appendChild(j);
  }catch(e){document.getElementById('map').style.display='none';document.getElementById('nomap').style.display='flex';}
}
// ── viewport mobile : 100dvh dans CSS suffit ──

initMap();
function centerMe(){if(map)map.setView([myLat,myLon],15);}

/* ═══════════════════════════════════════════════════════════
   ETAT
   ═══════════════════════════════════════════════════════════ */
var COLORS=['#4fc3f7','#66bb6a','#ffa726','#ef5350','#ce93d8','#26c6da','#ff7043','#ffee58'];
var drones={},colorIdx=0,selId=null,myselfId=null;
var protoCounts={FR:0,ODID:0,DJI:0,PAR:0};
var ws=null,simIv=null;
var bleDevice=null,bleTx=null,bleRx=null,bleBuf='';
var MYSELF_TOL=0.0003;
var myAlt=0;

function distDeg(lat1,lon1,lat2,lon2){return Math.sqrt((lat1-lat2)*(lat1-lat2)+(lon1-lon2)*(lon1-lon2));}

/* ═══════════════════════════════════════════════════════════
   BLE — Web Bluetooth (Nordic UART Service)
   ═══════════════════════════════════════════════════════════ */
var BLE_SVC='6e400001-b5a3-f393-e0a9-e50e24dcca9e';
var BLE_TX='6e400003-b5a3-f393-e0a9-e50e24dcca9e';
var BLE_RX='6e400002-b5a3-f393-e0a9-e50e24dcca9e';
var bleGPS=null;

async function connectBLE(){
  if(!navigator.bluetooth){document.getElementById('connInfo').textContent='Bluetooth non disponible';return;}
  try{
    bleDevice=await navigator.bluetooth.requestDevice({
      filters:[{services:[BLE_SVC]}],
      optionalServices:[BLE_SVC]
    });
    bleDevice.addEventListener('gattserverdisconnected',onBLEDisconnect);
    document.getElementById('connInfo').textContent='Connexion GATT...';
    var server=await bleDevice.gatt.connect();
    var svc=await server.getPrimaryService(BLE_SVC);
    bleTx=await svc.getCharacteristic(BLE_TX);
    bleRx=await svc.getCharacteristic(BLE_RX);
    await bleTx.startNotifications();
    bleTx.addEventListener('characteristicvaluechanged',onBLEData);
    setSt('ble','BLE connecte');
    document.getElementById('btnBLE').style.display='none';
    document.getElementById('btnDBLE').style.display='';
    document.getElementById('connInfo').textContent='BLE actif';
    // Envoyer GPS toutes les 2s
    bleGPS=setInterval(function(){
      if(!bleDevice||!bleDevice.gatt.connected)return;
      var lat=myLat,lon=myLon;
      if(map){var c=map.getCenter();lat=c.lat;lon=c.lng;}
      bleSend('{"g":['+lat.toFixed(6)+','+lon.toFixed(6)+','+Math.round(myAlt)+']}');
    },2000);
  }catch(e){
    setSt('off','BLE: '+e.message);
    document.getElementById('connInfo').textContent='Erreur BLE';
  }
}

function onBLEData(e){
  var val=new TextDecoder().decode(e.target.value);
  bleBuf+=val;
  var lines=bleBuf.split('\n');
  bleBuf=lines.pop();
  for(var i=0;i<lines.length;i++){
    var l=lines[i].trim();
    if(l.length>0&&l[0]=='{')processMessage(l);
  }
}

function onBLEDisconnect(){
  setSt('off','BLE deconnecte');
  document.getElementById('btnBLE').style.display='';
  document.getElementById('btnDBLE').style.display='none';
  document.getElementById('connInfo').textContent='Aucune connexion';
  if(bleGPS){clearInterval(bleGPS);bleGPS=null;}
  bleTx=null;bleRx=null;
}

async function disconnBLE(){
  if(bleGPS){clearInterval(bleGPS);bleGPS=null;}
  try{if(bleDevice&&bleDevice.gatt.connected)bleDevice.gatt.disconnect();}catch(e){}
  bleDevice=null;bleTx=null;bleRx=null;
  setSt('off','BLE deconnecte');
  document.getElementById('btnBLE').style.display='';
  document.getElementById('btnDBLE').style.display='none';
  document.getElementById('connInfo').textContent='Aucune connexion';
}

async function bleSend(msg){
  if(!bleRx||!bleDevice||!bleDevice.gatt.connected)return;
  try{
    var data=new TextEncoder().encode(msg);
    await bleRx.writeValueWithoutResponse(data);
  }catch(e){console.log('BLE TX err:',e);}
}

/* ═══════════════════════════════════════════════════════════
   WEBSOCKET
   ═══════════════════════════════════════════════════════════ */
function connectWS(){
  // FERMER AVANT D'OUVRIR
  try{if(ws){ws.onclose=null;ws.close();ws=null;}}catch(e){}
  try{var wsHost=(location.hostname&&location.hostname!=='')?location.hostname:'drone.local';
    ws=new WebSocket('ws://'+wsHost+':81');
    ws.onopen=function(){setSt('ws','WebSocket connecte');
      document.getElementById('btnWS').style.display='none';
      document.getElementById('btnDWS').style.display='';
      document.getElementById('connInfo').textContent='WiFi WebSocket actif';};
    ws.onmessage=function(e){processMessage(e.data);};
    ws.onclose=function(){disconnWS();};
    ws.onerror=function(){setSt('off','Erreur WS');};
  }catch(e){setSt('off','WS: '+e.message);}
}
function disconnWS(){
  try{if(ws){ws.onclose=null;ws.close();ws=null;}}catch(e){ws=null;}
  setSt('off','WS deconnecte');
  document.getElementById('btnWS').style.display='';
  document.getElementById('btnDWS').style.display='none';
  document.getElementById('connInfo').textContent='Aucune connexion';
}
function setSt(type,txt){
  var d=document.getElementById('dot');
  d.className='dot'+(type=='ble'?' ble':type=='ws'?' ws':' off');
  document.getElementById('status').textContent=txt;
}

/* ═══════════════════════════════════════════════════════════
   DECODEURS — FR / ODID / DJI / PAR
   ═══════════════════════════════════════════════════════════ */
function parseHex(h){h=h.replace(/[^0-9a-fA-F]/g,'');var b=[];for(var i=0;i<h.length;i+=2)b.push(parseInt(h.substr(i,2),16));return b;}
function rI32(b,o){return(b[o]<<24)|(b[o+1]<<16)|(b[o+2]<<8)|b[o+3];}
function rI16(b,o){var v=(b[o]<<8)|b[o+1];if(v>=0x8000)v-=0x10000;return v;}
function rU16(b,o){return(b[o]<<8)|b[o+1];}

function decodeFR(bytes){
  var r={proto:'FR'};var i=0;
  while(i<bytes.length-1){var t=bytes[i++];if(i>=bytes.length)break;var l=bytes[i++];if(i+l>bytes.length)break;var v=bytes.slice(i,i+l);i+=l;
    switch(t){case 1:r.version=v[0];break;case 2:r.idFR=String.fromCharCode.apply(null,v).replace(/\0/g,'');break;case 3:r.idANSI=String.fromCharCode.apply(null,v).replace(/\0/g,'');break;
    case 4:r.lat=rI32(v,0)/1e5;break;case 5:r.lon=rI32(v,0)/1e5;break;case 6:r.altMSL=rI16(v,0);break;case 7:r.altAGL=rI16(v,0);break;
    case 8:r.homeLat=rI32(v,0)/1e5;break;case 9:r.homeLon=rI32(v,0)/1e5;break;case 10:r.speed=v[0];break;case 11:r.heading=Math.round(v[0]*360/256);break;}}
  r.id=r.idFR||r.idANSI||'FR-?';r.idType=r.idFR?'France (SGDSN)':(r.idANSI?'ANSI/CTA':'');r.uaType='';return r;}

var ODID_IDT=['Aucun','Serial(ANSI)','Serial(CAA)','UTM(UUID)','Spec'];
var ODID_UAT=['?','Aeronef','Helico','Gyro','VTOL','CTOL','Fixe','Orni','Planeur','Cerf-v','Aerostat','Dirigeable','Parachute','Fusee','Attache','Sol'];

function decodeODID(bytes){
  var r={proto:'ODID',id:'ODID-?'};
  function leI32(b,o){return b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(b[o+3]<<24);}
  function leU16(b,o){return b[o]|(b[o+1]<<8);}
  var i=4;
  while(i+25<=bytes.length){var mb=bytes[i];var mt=(mb>>4)&0x0F;var msg=bytes.slice(i,i+25);i+=25;
    switch(mt){
    case 0:var it=(msg[1]>>4)&0x0F;var ut=msg[1]&0x0F;var rid=String.fromCharCode.apply(null,msg.slice(2,22)).replace(/\0/g,'');if(rid.length>0)r.id=rid;r.idType=ODID_IDT[it]||('T'+it);r.uaType=ODID_UAT[ut]||('T'+ut);break;
    case 1:var lat=leI32(msg,5)/1e7;var lon=leI32(msg,9)/1e7;var ag=(leU16(msg,13)*.5)-1000;var ht=(leU16(msg,17)*.5)-1000;
      var hs=msg[3]*.25;if(msg[1]&1)hs=(msg[3]*0.75)+63.75;var dr=msg[2]+((msg[1]&2)?180:0);
      if(lat!==0&&lon!==0){r.lat=lat;r.lon=lon;}if(ag>-999)r.altMSL=Math.round(ag);if(ht>-999)r.altAGL=Math.round(ht);r.speed=Math.round(hs*10)/10;r.heading=Math.round(dr)%360;break;
    case 4:var ol=leI32(msg,2)/1e7;var oo=leI32(msg,6)/1e7;if(ol!==0&&oo!==0){r.homeLat=ol;r.homeLon=oo;}break;
    case 5:var oi=String.fromCharCode.apply(null,msg.slice(2,22)).replace(/\0/g,'');if(oi.length>0&&r.id==='ODID-?')r.id=oi;break;
    }}return r;}

function decodeDJI(bytes){
  var r={proto:'DJI',id:'DJI-?'};if(bytes.length<40){var a='';for(var i=0;i<Math.min(bytes.length,50);i++){if(bytes[i]>=32&&bytes[i]<127)a+=String.fromCharCode(bytes[i]);else if(a.length>=6)break;else a='';}if(a.length>=6)r.id=a;return r;}
  try{var sn='';for(var i=6;i<22;i++){if(bytes[i]>=32&&bytes[i]<127)sn+=String.fromCharCode(bytes[i]);}if(sn.length>=6)r.id=sn;
  var lo=rI32(bytes,22)/1e7;var la=rI32(bytes,26)/1e7;if(la!==0&&lo!==0&&Math.abs(la)<90&&Math.abs(lo)<180){r.lat=la;r.lon=lo;}
  var al=rI16(bytes,30);var ht=rI16(bytes,32);if(al>-500&&al<10000)r.altMSL=al;if(ht>-500&&ht<10000)r.altAGL=ht;
  var vn=rI16(bytes,34)/100;var ve=rI16(bytes,36)/100;r.speed=Math.round(Math.sqrt(vn*vn+ve*ve)*10)/10;r.heading=Math.round((Math.atan2(ve,vn)*180/Math.PI+360)%360);
  var hlo=rI32(bytes,40)/1e7;var hla=rI32(bytes,44)/1e7;if(hla!==0&&hlo!==0&&Math.abs(hla)<90){r.homeLat=hla;r.homeLon=hlo;}}catch(e){}
  r.idType='DJI Serial';r.uaType='DJI';return r;}

function decodePAR(bytes){var r={proto:'PAR',id:'PAR-?'};var a='';for(var i=0;i<Math.min(bytes.length,60);i++){if(bytes[i]>=32&&bytes[i]<127)a+=String.fromCharCode(bytes[i]);else if(a.length>=6)break;else a='';}if(a.length>=6)r.id=a;r.idType='Parrot';r.uaType='Parrot';return r;}

function decodePayload(p,h){var b=parseHex(h);if(b.length<2)return null;switch(p){case'FR':return decodeFR(b);case'ODID':return decodeODID(b);case'DJI':return decodeDJI(b);case'PAR':return decodePAR(b);default:return null;}}

/* ═══════════════════════════════════════════════════════════
   TRAITEMENT MESSAGE
   ═══════════════════════════════════════════════════════════ */
function processMessage(raw){
  var pr,hex,mac,rssi,ch;
  try{var j=JSON.parse(raw);pr=j.p||'FR';hex=j.h||'';mac=j.m||'';rssi=j.r;ch=j.c;}catch(e){return;}
  if(pr!=='FR'&&pr!=='ODID'&&pr!=='DJI'&&pr!=='PAR')return;
  var d=decodePayload(pr,hex);if(!d)return;
  var meta={mac:mac,rssi:rssi,ch:ch,hex:hex};
  protoCounts[pr]=(protoCounts[pr]||0)+1;
  updateCounters();updateMap(d,meta);
}
function updateCounters(){
  document.getElementById('cFR').textContent=protoCounts.FR;
  document.getElementById('cODID').textContent=protoCounts.ODID;
  document.getElementById('cDJI').textContent=protoCounts.DJI;
  document.getElementById('cPAR').textContent=protoCounts.PAR;
}

/* ═══════════════════════════════════════════════════════════
   AFFICHAGE DECODE
   ═══════════════════════════════════════════════════════════ */
function updateDisplay(d,m){
  document.getElementById('vP').textContent=d.proto;
  document.getElementById('vMAC').textContent=m.mac||'-';
  document.getElementById('vRSSI').textContent=m.rssi!==undefined?m.rssi+' dBm':'-';
  document.getElementById('vCH').textContent=m.ch||'-';
  document.getElementById('vID').textContent=d.id||'-';
  document.getElementById('vIDT').textContent=d.idType||'-';
  document.getElementById('vUAT').textContent=d.uaType||'-';
  document.getElementById('vLat').textContent=d.lat!==undefined?d.lat.toFixed(5)+'\u00B0':'-';
  document.getElementById('vLon').textContent=d.lon!==undefined?d.lon.toFixed(5)+'\u00B0':'-';
  document.getElementById('vAlt').textContent=d.altMSL!==undefined?d.altMSL+' m':'-';
  document.getElementById('vAGL').textContent=d.altAGL!==undefined?d.altAGL+' m':'-';
  document.getElementById('vHLat').textContent=d.homeLat!==undefined?d.homeLat.toFixed(5)+'\u00B0':'-';
  document.getElementById('vHLon').textContent=d.homeLon!==undefined?d.homeLon.toFixed(5)+'\u00B0':'-';
  document.getElementById('vSpd').textContent=d.speed!==undefined?d.speed+' m/s':'-';
  document.getElementById('vHdg').textContent=d.heading!==undefined?d.heading+'\u00B0':'-';
  document.getElementById('vVs').textContent=d.vspeed!==undefined?d.vspeed+' m/s':'-';
  document.getElementById('vHex').textContent=m.hex||'-';
}

/* ═══════════════════════════════════════════════════════════
   CARTE
   ═══════════════════════════════════════════════════════════ */
function makeDroneIcon(c){
  return L.divIcon({className:'',html:'<svg width="24" height="24" viewBox="0 0 24 24"><path d="M12 2L4 12l8 4 8-4L12 2z" fill="'+c+'" stroke="#000" stroke-width="0.5" opacity="0.9"/><circle cx="12" cy="12" r="3" fill="#fff" opacity="0.8"/></svg>',iconSize:[24,24],iconAnchor:[12,12]});
}
function makeMyselfIcon(){
  return L.divIcon({className:'',html:'<svg width="28" height="28" viewBox="0 0 28 28"><circle cx="14" cy="14" r="12" fill="none" stroke="#ff0" stroke-width="2" opacity="0.9"/><path d="M14 4L6 14l8 4 8-4L14 4z" fill="#ff0" stroke="#000" stroke-width="0.5" opacity="0.9"/><circle cx="14" cy="14" r="3" fill="#fff" opacity="0.9"/></svg>',iconSize:[28,28],iconAnchor:[14,14]});
}
function makeHomeIcon(c){
  return L.divIcon({className:'',html:'<svg width="16" height="16" viewBox="0 0 16 16"><rect x="2" y="6" width="12" height="9" fill="'+c+'" opacity="0.5" stroke="'+c+'" rx="1"/><polygon points="8,1 1,7 15,7" fill="'+c+'" opacity="0.7"/></svg>',iconSize:[16,16],iconAnchor:[8,14]});
}

/* ═══════════════════════════════════════════════════════════
   TRAJECTOIRE 3D — Extrapolation par télémétrie native
   Utilise speed + heading du drone (pas dérivé de la position)
   Taux de virage = variation du heading entre updates
   Correction cos(lat) sur la longitude
   Cône d'incertitude entre 2 prédictions successives
   ═══════════════════════════════════════════════════════════ */
var PRED_SECS=60;       // secondes de prédiction
var PRED_STEPS=30;      // points sur l'arc (2 par seconde)
var PRED_WARN_M=200;    // alerte anticollision en mètres
var DEG_TO_M=111320;    // mètres par degré de latitude

// Distance en mètres entre 2 points lat/lon (Haversine)
function distM(lat1,lon1,lat2,lon2){
  var dLat=(lat2-lat1)*Math.PI/180;var dLon=(lon2-lon1)*Math.PI/180;
  var a=Math.sin(dLat/2)*Math.sin(dLat/2)+Math.cos(lat1*Math.PI/180)*Math.cos(lat2*Math.PI/180)*Math.sin(dLon/2)*Math.sin(dLon/2);
  return 6371000*2*Math.atan2(Math.sqrt(a),Math.sqrt(1-a));
}

// Point de référence pour la distance (myself drone ou moi-même)
function refPos(){
  if(myselfId&&drones[myselfId]&&drones[myselfId].data.lat!==undefined)
    return{lat:drones[myselfId].data.lat,lon:drones[myselfId].data.lon};
  return{lat:myLat,lon:myLon};
}

// Couleur du cône selon distance
function coneColor(dm){
  if(dm<100)return'#ef5350';   // rouge
  if(dm<200)return'#ff5722';   // rouge-orange
  if(dm<500)return'#ffa726';   // orange
  if(dm<1000)return'#ffee58';  // jaune
  if(dm<2000)return'#66bb6a';  // vert
  return'#4fc3f7';              // bleu
}

function updatePrediction(id){
  var dr=drones[id];if(!dr||!mapReady)return;
  if(dr.predLine){map.removeLayer(dr.predLine);dr.predLine=null;}
  if(dr.predPoly){map.removeLayer(dr.predPoly);dr.predPoly=null;}
  var h=dr.history;
  if(h.length<2)return;
  var n=h.length;
  var last=h[n-1];

  // ── Vitesse et cap depuis la télémétrie native ──
  var spdMs=last.speed;   // m/s (du décodeur)
  var hdgDeg=last.heading; // degrés (du décodeur)
  if(spdMs===undefined||spdMs<0.3)return; // immobile ou pas de donnée
  if(hdgDeg===undefined)return;
  var hdgRad=hdgDeg*Math.PI/180; // 0=Nord, sens horaire

  // ── Taux de virage depuis les headings successifs ──
  var turnRate=0; // rad/s
  if(n>=2){
    var prev=h[n-2];
    if(prev.heading!==undefined){
      var dt=(last.t-prev.t)/1000;
      if(dt>0.1){
        var dh=(last.heading-prev.heading);
        // Normaliser -180..+180
        while(dh>180)dh-=360;while(dh<-180)dh+=360;
        turnRate=(dh*Math.PI/180)/dt;
      }
    }
  }
  // Lisser avec le point N-2 si disponible
  if(n>=3){
    var pp=h[n-3];
    if(pp.heading!==undefined){
      var dt2=(last.t-pp.t)/1000;
      if(dt2>0.2){
        var dh2=(last.heading-pp.heading);
        while(dh2>180)dh2-=360;while(dh2<-180)dh2+=360;
        var tr2=(dh2*Math.PI/180)/dt2;
        turnRate=(turnRate+tr2)/2; // moyenne
      }
    }
  }

  // ── Correction cos(lat) pour la longitude ──
  var cosLat=Math.cos(last.lat*Math.PI/180);

  // ── Simulation pas-à-pas en mètres → degrés ──
  var ref=refPos();
  var pts=[];var minDist=Infinity;var minDistTime=0;
  var curLat=last.lat,curLon=last.lon,curHdg=hdgRad;
  var stepDt=PRED_SECS/PRED_STEPS;
  for(var s=1;s<=PRED_STEPS;s++){
    curHdg+=turnRate*stepDt;
    // Avancer en mètres, convertir en degrés
    var dN=spdMs*Math.cos(curHdg)*stepDt; // mètres Nord
    var dE=spdMs*Math.sin(curHdg)*stepDt; // mètres Est
    curLat+=dN/DEG_TO_M;
    curLon+=dE/(DEG_TO_M*cosLat);
    pts.push([curLat,curLon]);
    var dm=distM(curLat,curLon,ref.lat,ref.lon);
    if(dm<minDist){minDist=dm;minDistTime=s*stepDt;}
  }

  // ── Dessiner la polyline de prédiction ──
  if(pts.length>0){
    var allPts=[[last.lat,last.lon]].concat(pts);
    var predColor=(minDist<PRED_WARN_M)?'#ef5350':'#ffa726';
    dr.predLine=L.polyline(allPts,{color:predColor,weight:2,opacity:0.6,dashArray:'6 4'}).addTo(map);
    var lbl=Math.round(minDist)+'m';
    if(minDist<PRED_WARN_M)lbl+=' dans '+Math.round(minDistTime)+'s';
    dr.predLine.bindTooltip(lbl,{permanent:false,className:'alt-label'});
  }

  // ── Cône d'incertitude entre prédiction précédente et actuelle ──
  if(dr.prevPredPts&&dr.prevPredPts.length>0&&pts.length>0){
    var prevP=dr.prevPredPts;
    var curP=pts;
    var pn=Math.min(prevP.length,curP.length);
    var polyPts=[];
    for(var i=0;i<pn;i++)polyPts.push(prevP[i]);
    for(var i=pn-1;i>=0;i--)polyPts.push(curP[i]);
    var cc=coneColor(minDist);
    dr.predPoly=L.polygon(polyPts,{color:cc,fillColor:cc,fillOpacity:0.15,weight:0}).addTo(map);
  }

  dr.prevPredPts=pts.slice();
  dr.minPredDist=minDist;
  checkProximityBeep(minDist);
}

function updateMap(d,m){
  if(d.lat===undefined||d.lon===undefined)return;
  var id=d.id||'?';
  var dr=drones[id];
  if(!dr){
    var c=COLORS[colorIdx%COLORS.length];colorIdx++;
    dr={color:c,proto:d.proto,marker:null,home:null,trail:null,altLbl:null,predLine:null,predPoly:null,prevPredPts:null,history:[],data:d,meta:m,lastSeen:Date.now()};
    if(mapReady)dr.trail=L.polyline([],{color:c,weight:2,opacity:.7,dashArray:'4 4'}).addTo(map);
    drones[id]=dr;updateDroneList();
    // ── V3.3 : auto-detect myself (position statique proche de Moi) ──
    if(!myselfId&&d.lat!==undefined&&d.lon!==undefined&&(d.speed===undefined||d.speed<=1)){
      if(distDeg(d.lat,d.lon,myLat,myLon)<MYSELF_TOL){myselfId=id;updateDroneList();}
    }
  }
  dr.data=d;dr.meta=m;dr.proto=d.proto;dr.lastSeen=Date.now();
  if(mapReady){
    var ll=[d.lat,d.lon];
    if(!dr.marker)dr.marker=L.marker(ll,{icon:id===myselfId?makeMyselfIcon():makeDroneIcon(dr.color)}).addTo(map);
    else{dr.marker.setLatLng(ll);dr.marker.setIcon(id===myselfId?makeMyselfIcon():makeDroneIcon(dr.color));}
    var at='';
    if(d.altMSL!==undefined)at=d.altMSL+'m MSL';else if(d.altAGL!==undefined)at=d.altAGL+'m AGL';
    if(at){if(dr.altLbl)map.removeLayer(dr.altLbl);dr.altLbl=L.marker(ll,{icon:L.divIcon({className:'alt-label',html:at,iconSize:null,iconAnchor:[-14,12]})}).addTo(map);}
    if(d.homeLat!==undefined&&d.homeLon!==undefined){
      var hl=[d.homeLat,d.homeLon];
      if(!dr.home)dr.home=L.marker(hl,{icon:makeHomeIcon(dr.color)}).addTo(map).bindTooltip('Home');
      else dr.home.setLatLng(hl);
    }
    if(dr.trail)dr.trail.addLatLng(ll);
    // ── V3.4 : historique 3 points + prédiction trajectoire ──
    var alt=d.altMSL||d.altAGL||0;
    dr.history.push({lat:d.lat,lon:d.lon,alt:alt,speed:d.speed,heading:d.heading,t:Date.now()});
    if(dr.history.length>3)dr.history.shift();
    if(id!==myselfId)updatePrediction(id);
    var tip='<b style="color:'+(id===myselfId?'#ff0':dr.color)+'">'+(id===myselfId?'[MY] ':'')+' ['+d.proto+'] '+id+'</b><br>';
    if(at)tip+=at+'<br>';if(d.speed!==undefined)tip+=d.speed+' m/s';
    if(m.rssi!==undefined)tip+='<br>RSSI:'+m.rssi+'dBm';
    if(dr.minPredDist!==undefined&&dr.minPredDist<10000)tip+='<br><span style="color:'+(dr.minPredDist<PRED_WARN_M?'#ef5350':'#ffa726')+'">Dist:'+Math.round(dr.minPredDist)+'m/'+PRED_SECS+'s</span>';
    dr.marker.bindTooltip(tip);
    if(!firstDroneCentered){map.setView(ll,15);firstDroneCentered=true;}
  }
  if(!selId){selId=id;updateDroneList();}
  if(id===selId)updateDisplay(d,m);
}

function updateDroneList(){
  var c=document.getElementById('droneList');
  var ids=Object.keys(drones);
  if(ids.length===0){c.innerHTML='<div style="color:#445;font-size:11px;margin-top:8px">Aucun</div>';return;}
  c.innerHTML='';
  ids.forEach(function(id){
    var d=drones[id];var div=document.createElement('div');
    var isMe=id===myselfId;
    div.className='de'+(id===selId?' sel':'')+(isMe?' myself':'');
    var si=id.length>12?id.substring(0,12)+'...':id;
    div.innerHTML='<span class="dc" style="background:'+(isMe?'#ff0':d.color)+'"></span>'+si+'<span class="dp" style="color:'+d.color+';border:1px solid '+d.color+'">'+d.proto+'</span>'+(isMe?'<span class="my-tag">MY</span>':'');
    div.onclick=function(){
      if(!myselfId){myselfId=id;}
      selId=id;updateDroneList();updateDisplay(d.data,d.meta);
      if(mapReady&&d.data.lat)map.panTo([d.data.lat,d.data.lon]);
    };
    c.appendChild(div);
  });
}

/* ═══════════════════════════════════════════════════════════
   METEO — Open-Meteo (CORS OK) — Current + Vent Altitude
   ═══════════════════════════════════════════════════════════ */
var meteoLoaded=false,meteoTimer=null;

function ensureMeteo(){
  if(!meteoLoaded){fetchMeteo();meteoLoaded=true;}
  if(!meteoTimer)meteoTimer=setInterval(fetchMeteo,300000);
}
function stopMeteoTimer(){
  if(meteoTimer){clearInterval(meteoTimer);meteoTimer=null;}
}

// Desktop connpanel button: toggle centre entre Info ↔ Meteo
function toggleMeteoPanel(){
  var isMeteo=(document.getElementById('meteo').style.display==='block');
  showTab(isMeteo?'decode':'meteo');
}

function fetchMeteo(){
  var lat=myLat,lon=myLon;
  if(map){var c=map.getCenter();lat=c.lat;lon=c.lng;}
  var mp=document.getElementById('meteo');
  mp.innerHTML='<div style="color:#4fc3f7">Chargement meteo '+lat.toFixed(3)+','+lon.toFixed(3)+'...</div>';

  var url='https://api.open-meteo.com/v1/forecast?latitude='+lat.toFixed(4)+'&longitude='+lon.toFixed(4)
    +'&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,cloud_cover,pressure_msl,surface_pressure,wind_speed_10m,wind_direction_10m,wind_gusts_10m'
    +'&hourly=temperature_2m,wind_speed_10m,wind_speed_80m,wind_speed_120m,wind_speed_180m,wind_direction_10m,wind_direction_80m,wind_direction_120m,wind_direction_180m,wind_gusts_10m,visibility,cloud_cover'
    +'&forecast_days=1&wind_speed_unit=ms&timezone=auto';

  fetch(url).then(function(r){return r.json();}).then(function(d){
    renderMeteo(mp,lat,lon,d);
  }).catch(function(e){
    mp.innerHTML='<div style="color:#ef5350">Erreur Open-Meteo : '+e.message+'</div>';
  });
}

function degToCard(d){var dirs=['N','NE','E','SE','S','SW','W','NW'];return dirs[Math.round(d/45)%8];}

// WMO Weather Code → texte
function wmoText(c){
  var t={0:'Clair',1:'Peu nuageux',2:'Partiellement couvert',3:'Couvert',
    45:'Brouillard',48:'Brouillard givrant',51:'Bruine legere',53:'Bruine',55:'Bruine forte',
    61:'Pluie legere',63:'Pluie',65:'Pluie forte',71:'Neige legere',73:'Neige',75:'Neige forte',
    80:'Averses legeres',81:'Averses',82:'Averses fortes',95:'Orage',96:'Orage+grele',99:'Orage+grele fort'};
  return t[c]||('Code '+c);
}

function goNogo(wSpd,wGust,vis){
  if(wSpd>10||wGust>15)return'no';
  if(vis!==null&&vis<1000)return'no';
  if(wSpd>7||wGust>12)return'mar';
  if(vis!==null&&vis<3000)return'mar';
  return'go';
}
function goLabel(g){return g==='go'?'GO':g==='mar'?'MARGINAL':'NO-GO';}

function renderMeteo(mp,lat,lon,d){
  var h='';
  var cur=d.current||{};
  var hourly=d.hourly||{};

  // ── GO/NO-GO ──
  var wSpd=cur.wind_speed_10m||0;
  var wGust=cur.wind_gusts_10m||0;
  // Chercher la visibilite dans hourly (current n'a pas visibility)
  var vis=null;
  if(hourly.time&&hourly.visibility){
    var now=new Date();var hi=0;
    for(var i=0;i<hourly.time.length;i++){if(new Date(hourly.time[i])<=now)hi=i;}
    vis=hourly.visibility[hi];
  }
  var gng=goNogo(wSpd,wGust,vis);
  h+='<span class="gng gng-'+gng+'">'+goLabel(gng)+'</span>';
  h+=' <span style="color:#778;font-size:10px">'+wmoText(cur.weather_code||0)+'</span>';

  // ── CONDITIONS ACTUELLES ──
  h+='<div class="ms">CONDITIONS ACTUELLES</div>';
  h+='<div class="wrow"><span class="wh">Temp</span><span class="wv">'+(cur.temperature_2m!==undefined?cur.temperature_2m:'?')+'&deg;C</span>';
  h+='<span style="color:#778;margin-left:8px">Ressenti '+(cur.apparent_temperature!==undefined?cur.apparent_temperature:'?')+'&deg;C</span></div>';
  h+='<div class="wrow"><span class="wh">Humid</span><span class="wv">'+(cur.relative_humidity_2m||'?')+'%</span></div>';
  h+='<div class="wrow"><span class="wh">Vent</span><span class="wv">'+wSpd.toFixed(1)+' m/s';
  if(wGust>0)h+=' raf '+wGust.toFixed(1);
  h+='</span><span class="wd"> '+degToCard(cur.wind_direction_10m||0)+' ('+(cur.wind_direction_10m||0)+'&deg;)</span></div>';
  h+='<div class="wrow"><span class="wh">QNH</span><span class="wv">'+(cur.pressure_msl?cur.pressure_msl.toFixed(0):'?')+' hPa</span>';
  h+='<span style="color:#778;margin-left:8px">QFE '+(cur.surface_pressure?cur.surface_pressure.toFixed(0):'?')+'</span></div>';
  if(vis!==null)h+='<div class="wrow"><span class="wh">Visi</span><span class="wv">'+(vis>=10000?(vis/1000).toFixed(0)+'km':vis+'m')+'</span></div>';
  h+='<div class="wrow"><span class="wh">Nuages</span><span class="wv">'+(cur.cloud_cover||0)+'%</span></div>';
  if(cur.precipitation>0)h+='<div class="wrow"><span class="wh">Precip</span><span class="wv" style="color:#ef5350">'+cur.precipitation+' mm</span></div>';

  // ── PREVISION 6H (vent + visi) ──
  h+='<div class="ms">PREVISION 6H</div>';
  if(hourly.time){
    var now=new Date();var hi=0;
    for(var i=0;i<hourly.time.length;i++){if(new Date(hourly.time[i])<=now)hi=i;}
    var to=Math.min(hourly.time.length-1,hi+6);
    h+='<div class="wrow" style="color:#556"><span class="wh">Heure</span><span style="min-width:40px">Vent</span><span style="min-width:30px">Raf</span><span style="min-width:35px">Dir</span><span style="min-width:40px">Visi</span><span style="min-width:30px">Nua</span></div>';
    for(var i=hi;i<=to;i++){
      var t=new Date(hourly.time[i]);
      var hh=('0'+t.getHours()).slice(-2)+':'+('0'+t.getMinutes()).slice(-2);
      var ws=hourly.wind_speed_10m?hourly.wind_speed_10m[i]:0;
      var wg=hourly.wind_gusts_10m?hourly.wind_gusts_10m[i]:0;
      var wd=hourly.wind_direction_10m?hourly.wind_direction_10m[i]:0;
      var vi=hourly.visibility?hourly.visibility[i]:99999;
      var cc=hourly.cloud_cover?hourly.cloud_cover[i]:0;
      var fg=goNogo(ws,wg,vi);
      var rc=fg==='no'?'#ef5350':fg==='mar'?'#ffa726':'#66bb6a';
      var isCur=(i===hi)?' style="background:#1a2533"':'';
      h+='<div class="wrow"'+isCur+'><span class="wh">'+(i===hi?'<b>'+hh+'</b>':hh)+'</span>';
      h+='<span style="min-width:40px;color:'+rc+'">'+ws.toFixed(1)+'</span>';
      h+='<span style="min-width:30px;color:#ffa726">'+wg.toFixed(0)+'</span>';
      h+='<span style="min-width:35px" class="wd">'+degToCard(wd)+'</span>';
      h+='<span style="min-width:40px;color:'+(vi<3000?'#ef5350':'#66bb6a')+'">'+(vi>=10000?(vi/1000).toFixed(0)+'k':vi)+'</span>';
      h+='<span style="min-width:30px;color:#778">'+cc+'%</span></div>';
    }
  }

  // ── VENT ALTITUDE ──
  h+='<div class="ms">VENT ALTITUDE</div>';
  if(hourly.time&&hourly.wind_speed_120m){
    var now=new Date();var hi=0;
    for(var i=0;i<hourly.time.length;i++){if(new Date(hourly.time[i])<=now)hi=i;}
    var from=Math.max(0,hi-1);
    var to=Math.min(hourly.time.length-1,hi+6);
    h+='<div class="wrow" style="color:#556"><span class="wh">Heure</span><span style="min-width:40px">10m</span><span style="min-width:40px">80m</span><span style="min-width:40px">120m</span><span style="min-width:40px">180m</span><span style="min-width:30px">Raf</span></div>';
    for(var i=from;i<=to;i++){
      var t=new Date(hourly.time[i]);
      var hh=('0'+t.getHours()).slice(-2)+':'+('0'+t.getMinutes()).slice(-2);
      var w10=hourly.wind_speed_10m?hourly.wind_speed_10m[i]:0;
      var w80=hourly.wind_speed_80m?hourly.wind_speed_80m[i]:0;
      var w120=hourly.wind_speed_120m?hourly.wind_speed_120m[i]:0;
      var w180=hourly.wind_speed_180m?hourly.wind_speed_180m[i]:0;
      var g10=hourly.wind_gusts_10m?hourly.wind_gusts_10m[i]:0;
      var d120=hourly.wind_direction_120m?hourly.wind_direction_120m[i]:0;
      var maxW=Math.max(w10,w80,w120,w180);
      var rc=maxW>10?'#ef5350':maxW>7?'#ffa726':'#66bb6a';
      var isCur=(i===hi)?' style="background:#1a2533"':'';
      h+='<div class="wrow"'+isCur+'><span class="wh">'+(i===hi?'<b>'+hh+'</b>':hh)+'</span>';
      h+='<span style="min-width:40px;color:'+rc+'">'+w10.toFixed(1)+'</span>';
      h+='<span style="min-width:40px;color:'+rc+'">'+w80.toFixed(1)+'</span>';
      h+='<span style="min-width:40px;color:'+rc+'">'+w120.toFixed(1)+'</span>';
      h+='<span style="min-width:40px;color:'+rc+'">'+w180.toFixed(1)+'</span>';
      h+='<span style="min-width:30px;color:#ffa726">'+g10.toFixed(1)+'</span>';
      h+='<span class="wd" style="min-width:25px">'+degToCard(d120)+'</span></div>';
    }
    h+='<div style="color:#556;font-size:9px;margin-top:2px">Vitesses en m/s</div>';
  } else {
    h+='<div style="color:#445">Vent altitude indisponible</div>';
  }

  mp.innerHTML=h;
}

/* ═══════════════════════════════════════════════════════════
   SIMULATION
   ═══════════════════════════════════════════════════════════ */
function encI32(v){v=Math.round(v);if(v<0)v=v+0x100000000;return[v>>>24,(v>>>16)&0xFF,(v>>>8)&0xFF,v&0xFF];}
function encI16(v){v=Math.round(v);if(v<0)v=v+0x10000;return[(v>>>8)&0xFF,v&0xFF];}

function buildFR(lat,lon,alt,spd,hdg,hLat,hLon,id){
  var b=[];b.push(1,1,1);var ib=[];for(var i=0;i<30;i++)ib.push(i<id.length?id.charCodeAt(i):0);b.push(2,30);b=b.concat(ib);
  b.push(4,4);b=b.concat(encI32(lat*1e5));b.push(5,4);b=b.concat(encI32(lon*1e5));b.push(6,2);b=b.concat(encI16(alt));
  b.push(8,4);b=b.concat(encI32(hLat*1e5));b.push(9,4);b=b.concat(encI32(hLon*1e5));
  b.push(10,1,spd&0xFF);b.push(11,1,Math.round(hdg*256/360)&0xFF);
  return b.map(function(x){return x.toString(16).padStart(2,'0');}).join('');}

function startSim(){
  if(simIv)return;clearDrones();
  var a1=0,a2=Math.PI,hLat=myLat+.001,hLon=myLon-.001,hLat2=myLat-.002,hLon2=myLon+.002;
  setSt('ble','Simulation');document.getElementById('btnStop').style.display='';
  simIv=setInterval(function(){
    a1+=.08;a2+=.05;
    processMessage(JSON.stringify({p:'FR',h:buildFR(hLat+Math.sin(a1)*.003,hLon+Math.cos(a1)*.003,80+Math.round(Math.sin(a1*.5)*20),5,Math.round(((-a1*180/Math.PI)%360+360)%360),hLat,hLon,'SIM-FR-DRONE01'),r:-55,m:'AA:BB:CC:DD:EE:01',c:6}));
    processMessage(JSON.stringify({p:'FR',h:buildFR(hLat2+Math.sin(a2)*.005,hLon2+Math.cos(a2)*.005,120+Math.round(Math.cos(a2)*15),8,Math.round(((-a2*180/Math.PI)%360+360)%360),hLat2,hLon2,'SIM-FR-DRONE02'),r:-72,m:'AA:BB:CC:DD:EE:02',c:11}));
  },1200);
}

function clearDrones(){
  Object.keys(drones).forEach(function(id){var d=drones[id];if(mapReady){if(d.marker)map.removeLayer(d.marker);if(d.home)map.removeLayer(d.home);if(d.trail)map.removeLayer(d.trail);if(d.altLbl)map.removeLayer(d.altLbl);if(d.predLine)map.removeLayer(d.predLine);if(d.predPoly)map.removeLayer(d.predPoly);}});
  drones={};colorIdx=0;selId=null;myselfId=null;firstDroneCentered=false;
  protoCounts={FR:0,ODID:0,DJI:0,PAR:0};updateCounters();updateDroneList();
}

function stopAll(){
  if(simIv){clearInterval(simIv);simIv=null;}
  clearDrones();
  if(adsbIv){clearInterval(adsbIv);adsbIv=null;adsbOn=false;}
  clearAircraft();
  disconnWS();disconnBLE();
  setSt('off','Inactif');document.getElementById('btnStop').style.display='none';
  var ab=document.getElementById('btnADSB');ab.style.background='';ab.style.borderColor='';
}

document.addEventListener('keydown',function(e){if(e.key==='Escape'){stopAll();hideTxConfig();}});

/* ═══════════════════════════════════════════════════════════
   SON — Toggle + beep navigateur (proximity)
   ═══════════════════════════════════════════════════════════ */
var soundOn=false;
var audioCtx=null;

function toggleSound(){
  soundOn=!soundOn;
  document.getElementById('btnSnd').innerHTML=soundOn?'&#128266;':'&#128264;';
  document.getElementById('btnSnd').style.color=soundOn?'#66bb6a':'#4fc3f7';
  if(soundOn&&!audioCtx)audioCtx=new(window.AudioContext||window.webkitAudioContext)();
  // Envoyer au board via BLE
  bleSend('{"snd":'+(soundOn?1:0)+'}');
}

function beepProximity(freqHz,durationMs){
  if(!soundOn||!audioCtx)return;
  var osc=audioCtx.createOscillator();var gain=audioCtx.createGain();
  osc.connect(gain);gain.connect(audioCtx.destination);
  osc.frequency.value=freqHz;osc.type='square';
  gain.gain.value=0.15;
  osc.start();osc.stop(audioCtx.currentTime+durationMs/1000);
}

// Beep dans updatePrediction quand distance < seuil
var lastBeepTime=0;
function checkProximityBeep(dist){
  if(!soundOn||dist>PRED_WARN_M)return;
  var now=Date.now();
  if(now-lastBeepTime<3000)return; // pas plus d'1 beep / 3s
  lastBeepTime=now;
  if(dist<100){beepProximity(1200,200);setTimeout(function(){beepProximity(1600,200);},250);}
  else{beepProximity(800,150);}
}

/* ═══════════════════════════════════════════════════════════
   ADS-B — AirLabs API (CORS OK)
   Avions dans la vue active de la carte
   ═══════════════════════════════════════════════════════════ */
var adsbOn=false,adsbIv=null;
var aircraft={};  // hex → {marker, label, data, lastSeen}
var ADSB_KEY='01d06a3e-18b7-49ca-933e-ce1dd3a771cb';
var ADSB_URL='https://airlabs.co/api/v9/flights';
var ADSB_INTERVAL=30000; // 30s (économie crédits AirLabs)

function toggleADSB(){
  adsbOn=!adsbOn;
  var btn=document.getElementById('btnADSB');
  btn.style.background=adsbOn?'#1a2a2a':'';
  btn.style.borderColor=adsbOn?'#26c6da':'';
  btn.style.color=adsbOn?'#26c6da':'';
  btn.textContent='ADS-B';
  if(adsbOn){
    fetchADSB();
    adsbIv=setInterval(fetchADSB,ADSB_INTERVAL);
  } else {
    if(adsbIv){clearInterval(adsbIv);adsbIv=null;}
    clearAircraft();
  }
}

function makeAircraftIcon(hdg,alt,ground){
  var col;
  if(ground) col='#ef5350';
  else col=alt>5000?'#26c6da':alt>2000?'#66bb6a':alt>500?'#ffa726':'#ef5350';
  var rot=hdg||0;
  return L.divIcon({className:'',html:'<svg width="24" height="24" viewBox="0 0 24 24" style="transform:rotate('+rot+'deg);transform-origin:50% 50%"><path d="M22 16.085v-1.543l-9.282-5.758v-6.323c0-1.127-.792-1.95-1.718-1.95-.926 0-1.718.823-1.718 1.95v6.323l-9.282 5.758v1.543l9.282-2.89v5.04l-2.43 1.838v1.427l3.148-1.042 3.148 1.042v-1.427l-2.43-1.838v-5.04l9.282 2.89z" fill="'+col+'" stroke="#222" stroke-width="0.5" opacity="0.9"/></svg>',iconSize:[24,24],iconAnchor:[12,12]});
}

function fetchADSB(){
  if(!map||!mapReady)return;
  var b=map.getBounds();
  var bbox=b.getSouth().toFixed(4)+','+b.getWest().toFixed(4)+','+b.getNorth().toFixed(4)+','+b.getEast().toFixed(4);
  var url=ADSB_URL+'?api_key='+ADSB_KEY+'&bbox='+bbox;

  var btn=document.getElementById('btnADSB');

  fetch(url,{signal:AbortSignal.timeout(10000)})
    .then(function(r){
      if(!r.ok){
        if(r.status===429){
          btn.style.background='#ef5350';btn.style.color='#fff';
          btn.style.borderColor='#ef5350';btn.textContent='ADS-B (429)';
        }
        throw new Error('HTTP '+r.status);
      }
      btn.style.background='#1a2a2a';btn.style.color='#26c6da';
      btn.style.borderColor='#26c6da';btn.textContent='ADS-B';
      return r.json();
    })
    .then(function(data){
      if(data.error){console.log('[ADSB] API error: '+data.error.message);return;}
      var flights=data.response||[];
      var seen={};

      flights.forEach(function(f){
        var icao=f.hex;
        if(!icao)return;
        var aLat=f.lat,aLon=f.lng;
        if(aLat===null||aLon===null||aLat===undefined||aLon===undefined)return;
        var call=f.flight_icao||f.reg_number||icao;
        var alt=f.alt||0;
        var spd=f.speed||0;
        var hdg=f.dir||0;
        var ground=(alt<50);
        seen[icao]=true;

        var ac=aircraft[icao];
        if(!ac){
          ac={marker:null,label:null,data:{}};
          aircraft[icao]=ac;
        }
        ac.data={call:call,lat:aLat,lon:aLon,alt:Math.round(alt),spd:Math.round(spd),hdg:Math.round(hdg)};
        ac.lastSeen=Date.now();

        var ll=[aLat,aLon];
        if(!ac.marker){
          ac.marker=L.marker(ll,{icon:makeAircraftIcon(hdg,alt,ground),zIndexOffset:-100}).addTo(map);
        } else {
          ac.marker.setLatLng(ll);
          ac.marker.setIcon(makeAircraftIcon(hdg,alt,ground));
        }

        var tip='<b style="color:#26c6da">'+call+'</b><br>'
          +Math.round(alt)+'m (FL'+Math.round(alt*3.28084/100)+')<br>'
          +Math.round(spd)+'km/h '+Math.round(hdg)+'&deg;'
          +(ground?'<br><b style="color:#ef5350">AU SOL</b>':'');
        var dm=distM(aLat,aLon,myLat,myLon);
        tip+='<br><span style="color:'+(dm<2000?'#ef5350':'#778')+'">'+Math.round(dm/100)/10+'km</span>';
        ac.marker.bindTooltip(tip);

        var altTxt=ground?'GND':'FL'+Math.round(alt*3.28084/100);
        if(!ac.label)ac.label=L.marker(ll,{icon:L.divIcon({className:'ac-label',html:altTxt,iconSize:null,iconAnchor:[-12,8]}),zIndexOffset:-100}).addTo(map);
        else{ac.label.setLatLng(ll);ac.label.setIcon(L.divIcon({className:'ac-label',html:altTxt,iconSize:null,iconAnchor:[-12,8]}));}
      });

      Object.keys(aircraft).forEach(function(icao){
        if(!seen[icao]&&aircraft[icao].lastSeen<Date.now()-60000){
          if(aircraft[icao].marker)map.removeLayer(aircraft[icao].marker);
          if(aircraft[icao].label)map.removeLayer(aircraft[icao].label);
          delete aircraft[icao];
        }
      });
    })
    .catch(function(e){
      console.log('[ADSB] '+e.message);
    });
}

function clearAircraft(){
  Object.keys(aircraft).forEach(function(icao){
    if(aircraft[icao].marker)map.removeLayer(aircraft[icao].marker);
    if(aircraft[icao].label)map.removeLayer(aircraft[icao].label);
  });
  aircraft={};
}

/* ═══════════════════════════════════════════════════════════
   TABS MOBILE — Bottom panel
   ═══════════════════════════════════════════════════════════ */
function showTab(id){
  var panels=document.querySelectorAll('.bpanel');
  for(var i=0;i<panels.length;i++){panels[i].classList.remove('active');}
  var btns=document.querySelectorAll('#btabs button');
  for(var i=0;i<btns.length;i++){btns[i].classList.remove('active');}
  var el=document.getElementById(id);
  if(el)el.classList.add('active');
  // Activer le bon tab button
  var names=['connpanel','decode','meteo','drones'];
  var idx=names.indexOf(id);
  if(idx>=0&&btns[idx])btns[idx].classList.add('active');
  // Centre panel: decode vs meteo (desktop + mobile)
  if(id==='meteo'){
    document.getElementById('decode').style.display='none';
    document.getElementById('meteo').style.display='block';
    ensureMeteo();
  } else {
    document.getElementById('decode').style.display='';
    document.getElementById('meteo').style.display='none';
    stopMeteoTimer();
  }
}

/* ═══════════════════════════════════════════════════════════
   TX CONFIG — Modal configuration émetteur
   Envoie la config au board via BLE JSON
   ═══════════════════════════════════════════════════════════ */
var txConfig={
  id:'FRAokesq0o9db0q6-cf8',
  uaType:2,idType:1,euCat:1,euClass:2,
  selfDesc:'Recreational flight'
};

function showTxConfig(){
  document.getElementById('txmodal').classList.add('show');
  document.getElementById('txId').value=txConfig.id;
  document.getElementById('txUaType').value=txConfig.uaType;
  document.getElementById('txIdType').value=txConfig.idType;
  document.getElementById('txEuCat').value=txConfig.euCat;
  document.getElementById('txEuClass').value=txConfig.euClass;
  document.getElementById('txDesc').value=txConfig.selfDesc;
}

function hideTxConfig(){
  document.getElementById('txmodal').classList.remove('show');
}

function saveTxConfig(){
  txConfig.id=document.getElementById('txId').value;
  txConfig.uaType=parseInt(document.getElementById('txUaType').value);
  txConfig.idType=parseInt(document.getElementById('txIdType').value);
  txConfig.euCat=parseInt(document.getElementById('txEuCat').value);
  txConfig.euClass=parseInt(document.getElementById('txEuClass').value);
  txConfig.selfDesc=document.getElementById('txDesc').value;
  // Envoyer au board via BLE
  var cmd=JSON.stringify({tx:txConfig});
  bleSend(cmd);
  console.log('TX>',cmd);
  hideTxConfig();
}
</script>
<!-- TX Config Modal -->
<div id="txmodal" onclick="if(event.target===this)hideTxConfig()">
<div id="txform">
  <div style="font-size:14px;color:#ffa726;font-weight:bold;margin-bottom:4px">TX DRONE ID — Configuration</div>
  <label>ID Exploitant (Alpha Tango)</label>
  <input type="text" id="txId" maxlength="20" placeholder="FRAxxxxx">
  <label>Type de drone</label>
  <select id="txUaType">
    <option value="0">Inconnu</option>
    <option value="1">Avion / Aile fixe</option>
    <option value="2">Helicoptere / Multirotor</option>
    <option value="3">Gyravion</option>
    <option value="4">VTOL (hybride)</option>
    <option value="5">CTOL (decollage court)</option>
    <option value="7">Ornithoptere</option>
    <option value="8">Planeur</option>
    <option value="10">Aerostat (ballon)</option>
    <option value="11">Dirigeable</option>
    <option value="12">Parachute</option>
    <option value="14">Sol (rover)</option>
  </select>
  <label>Type d'identification</label>
  <select id="txIdType">
    <option value="0">Aucun</option>
    <option value="1">Numero de serie (ANSI)</option>
    <option value="2">Numero de serie (CAA)</option>
    <option value="3">UTM (UUID)</option>
    <option value="4">Specifique</option>
  </select>
  <label>Categorie EU</label>
  <select id="txEuCat">
    <option value="0">Non defini</option>
    <option value="1">Open</option>
    <option value="2">Specific</option>
    <option value="3">Certified</option>
  </select>
  <label>Classe EU</label>
  <select id="txEuClass">
    <option value="0">Non defini</option>
    <option value="1">C0</option>
    <option value="2">C1</option>
    <option value="3">C2</option>
    <option value="4">C3</option>
    <option value="5">C4</option>
    <option value="6">C5</option>
    <option value="7">C6</option>
  </select>
  <label>Description operation</label>
  <input type="text" id="txDesc" maxlength="23" placeholder="Recreational flight">
  <div class="txbtns">
    <button style="background:#2a1a1a;color:#ef5350;border-color:#4a2a2d" onclick="hideTxConfig()">Annuler</button>
    <button style="background:#1a2a1a;color:#66bb6a;border-color:#2a4a2d" onclick="saveTxConfig()">Envoyer</button>
  </div>
</div>
</div>
</body>
</html>)rawpage";

#endif
