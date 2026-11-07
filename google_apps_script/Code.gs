/**
 * ==============================================================================
 * Project: Campus Safety & Foot-Traffic Telemetry Web App Backend (Grid Edition)
 * Features:
 *   1. doPost(e): รับข้อมูล Telemetry จาก ESP32 ลง Google Sheets + High-Speed Cache + LINE Alert
 *   2. doGet(e): ให้บริการหน้า Web Dashboard สไตล์ Smart Campus แบบ Real-time
 * ==============================================================================
 */

const LINE_NOTIFY_TOKEN = ""; // ใส่ LINE Notify Token ของคุณที่นี่ (ถ้ามี)

function doPost(e) {
  try {
    const payload = JSON.parse(e.postData.contents);

    const now = new Date();
    const timestamp = Utilities.formatDate(now, "GMT+7", "yyyy-MM-dd HH:mm:ss");
    const nodeId       = payload.node_id || "NODE-GYM7-01";
    const trafficCount = Number(payload.traffic_count || 0);
    const lightStatus  = String(payload.light_status || "OFF");
    const powerWatts   = Number(payload.power_watts || 0.0);
    const alertStatus  = String(payload.alert_status || "NORMAL");

    // 1. เก็บลง High-Speed Script Cache ทันที เพื่อให้ Dashboard ดึงแบบ Real-time (< 20ms)
    try {
      const cacheObj = {
        nodeId: nodeId,
        latestTimestamp: timestamp,
        totalTraffic: trafficCount,
        lightStatus: lightStatus,
        powerWatts: powerWatts.toFixed(1),
        alertStatus: alertStatus
      };
      CacheService.getScriptCache().put("LATEST_REALTIME_STATS", JSON.stringify(cacheObj), 21600);
    } catch (cacheErr) {
      Logger.log("Cache write error: " + cacheErr);
    }

    // 2. บันทึกข้อมูลลง Google Sheet แบบถาวร
    const sheet = getOrCreateDataSheet();
    sheet.appendRow([timestamp, nodeId, trafficCount, lightStatus, powerWatts, alertStatus]);

    // 3. แจ้งเตือน LINE หากเกิด Loitering
    if (alertStatus === "LOITERING_ALERT" && LINE_NOTIFY_TOKEN.trim() !== "") {
      const lineMsg = "\n🚨 [เตือนภัยจุดอับยามวิกาล - Smart Campus]\n" +
                      "ตรวจพบการป้วนเปี้ยนผิดปกติบริเวณทางเดินยิม 7\n" +
                      "📍 เสาโหนด: " + nodeId + "\n" +
                      "⏰ เวลา: " + Utilities.formatDate(now, "GMT+7", "HH:mm:ss") + "\n" +
                      "💡 สถานะไฟ: " + lightStatus + " (" + powerWatts + "W)\n" +
                      "⚠️ ระบบเร่งไฟ 100% เรียบร้อยแล้ว กรุณาส่งสายตรวจเข้าพื้นที่";
      sendLineNotify(lineMsg);
    }

    return ContentService.createTextOutput(JSON.stringify({
      "status": "SUCCESS",
      "received_at": timestamp
    })).setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return ContentService.createTextOutput(JSON.stringify({
      "status": "ERROR",
      "message": err.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

function doGet(e) {
  if (e && e.parameter && e.parameter.action === "getLatest") {
    const data = fetchLatestStats();
    return ContentService.createTextOutput(JSON.stringify(data))
                         .setMimeType(ContentService.MimeType.JSON);
  }

  return HtmlService.createHtmlOutputFromFile('Dashboard')
                    .setTitle('Campus Safety Command Center | Gym 7')
                    .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL)
                    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

// Function สำหรับ Client-side google.script.run (ส่งเป็น String ปลอดภัย 100% ไม่ติด Date serialization)
function getLatestStatsJson() {
  return JSON.stringify(fetchLatestStats());
}

function fetchLatestStats() {
  // ดึงจาก In-Memory Cache ก่อน เพื่อความเร็วสูงสุดระดับ Real-time
  let cached = null;
  try {
    const cacheStr = CacheService.getScriptCache().get("LATEST_REALTIME_STATS");
    if (cacheStr) {
      cached = JSON.parse(cacheStr);
    }
  } catch (e) {
    cached = null;
  }

  const sheet = getOrCreateDataSheet();
  const lastRow = sheet.getLastRow();

  if (lastRow <= 1) {
    return cached || {
      nodeId: "NODE-GYM7-01",
      latestTimestamp: Utilities.formatDate(new Date(), "GMT+7", "yyyy-MM-dd HH:mm:ss"),
      totalTraffic: 0,
      lightStatus: "STANDBY",
      powerWatts: "0.6",
      alertStatus: "NORMAL",
      recentLogs: []
    };
  }

  const latestRow = sheet.getRange(lastRow, 1, 1, 6).getValues()[0];
  const numRows = Math.min(10, lastRow - 1);
  const startRow = lastRow - numRows + 1;
  const recentLogs = sheet.getRange(startRow, 1, numRows, 6).getValues().reverse();

  let formattedTime = (cached && cached.latestTimestamp) ? cached.latestTimestamp : "";
  if (!formattedTime) {
    try {
      formattedTime = Utilities.formatDate(new Date(latestRow[0]), "GMT+7", "yyyy-MM-dd HH:mm:ss");
    } catch (e) {
      formattedTime = String(latestRow[0]);
    }
  }

  return {
    latestTimestamp: formattedTime,
    nodeId: (cached && cached.nodeId) || String(latestRow[1] || "NODE-GYM7-01"),
    totalTraffic: (cached && cached.totalTraffic !== undefined) ? cached.totalTraffic : Number(latestRow[2] || 0),
    lightStatus: (cached && cached.lightStatus) || String(latestRow[3] || "OFF"),
    powerWatts: (cached && cached.powerWatts) || String(Number(latestRow[4] || 0.6).toFixed(1)),
    alertStatus: (cached && cached.alertStatus) || String(latestRow[5] || "NORMAL"),
    recentLogs: recentLogs.map(function(row) {
      let rowTime = "";
      try {
        rowTime = Utilities.formatDate(new Date(row[0]), "GMT+7", "HH:mm:ss");
      } catch (err) {
        rowTime = String(row[0]);
      }
      return {
        time: rowTime,
        node: String(row[1] || "NODE-GYM7-01"),
        count: Number(row[2] || 0),
        light: String(row[3] || "OFF"),
        power: Number(row[4] || 0).toFixed(1) + " W",
        alert: String(row[5] || "NORMAL")
      };
    })
  };
}

function getOrCreateDataSheet() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName("TelemetryLogs");
  if (!sheet) {
    sheet = ss.insertSheet("TelemetryLogs");
    sheet.appendRow(["Timestamp", "Node_ID", "Traffic_Count", "Light_Status", "Power_Watts", "Alert_Status"]);
    sheet.setFrozenRows(1);
    sheet.getRange("A1:F1").setFontWeight("bold").setBackground("#0F172A").setFontColor("#38BDF8");
  }
  return sheet;
}

function sendLineNotify(message) {
  try {
    const url = "https://notify-api.line.me/api/notify";
    const options = {
      "method": "post",
      "headers": { "Authorization": "Bearer " + LINE_NOTIFY_TOKEN },
      "payload": { "message": message },
      "muteHttpExceptions": true
    };
    UrlFetchApp.fetch(url, options);
  } catch (e) {
    Logger.log("LINE Notify Error: " + e.toString());
  }
}
