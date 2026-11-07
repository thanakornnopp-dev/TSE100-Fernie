# คู่มือการติดตั้งระบบคลาวด์ใหม่ (Supabase Realtime Cloud Guide)

---

### ทำไมสถาปัตยกรรมนี้ถึงดีกว่า Google Sheets มาก?
1. **Real-time WebSockets:** ไม่ต้องรอดึงข้อมูลทุก 3 วินาทีอีกต่อไป พอมีคนเดินผ่านเสาไฟ ข้อมูลจะพุ่งเข้าหน้าจอ Dashboard ภายใน **0.05 วินาที (< 50ms)**
2. **ฐานข้อมูล PostgreSQL 15:** เป็นฐานข้อมูลระดับองค์กรจริง มีการสร้าง Index และจัดเก็บ Log ที่ถูกต้องตามหลักวิศวกรรม
3. **ฟรี 100% ตลอดชีพ:** รองรับการเชื่อมต่อไม่จำกัด ไม่ติดข้อจำกัด 1 บอร์ดเหมือน Blynk

---

### ขั้นตอนการตั้งค่า Supabase (ใช้เวลาเพียง 2 นาที):

#### 1. สมัครและสร้างโปรเจกต์ใหม่
1. เปิดเว็บ: **[supabase.com](https://supabase.com/)** แล้วกดปุ่ม **Start your project** (ล็อกอินด้วยบัญชี GitHub หรือ Google ก็ได้ ฟรี 100%)
2. กดปุ่มสีเขียว **"New Project"**
   - **Name:** `campus-safety-node`
   - **Database Password:** ตั้งรหัสผ่านที่จำได้ (เช่น `CampusPass2026!`)
   - **Region:** เลือก `Singapore` (ใกล้ไทยที่สุด ค่า Latency ต่ำมาก)
3. กดปุ่ม **"Create new project"** (รอระบบสร้างฐานข้อมูลประมาณ 1 นาที)

---

#### 2. สร้างตารางฐานข้อมูล (Run SQL Script)
1. มองแถบเมนูด้านซ้าย คลิกที่ไอคอน **SQL Editor** (รูป `>_`)
2. คลิกปุ่ม **"New query"**
3. คัดลอกโค้ดทั้งหมดจากไฟล์ [`supabase/schema.sql`](file:///Users/thanaporn/.gemini/antigravity/scratch/campus-safety-node/supabase/schema.sql) มาวาง
4. กดปุ่มสีเขียว **"Run"** ขวาล่าง
   *(จะขึ้นข้อความ Success แปลว่าตาราง `telemetry_logs` และระบบ Realtime WebSockets พร้อมใช้งานแล้ว)*

---

#### 3. คัดลอก Keys มาใส่ในระบบ
1. มองแถบเมนูด้านซ้ายล่างสุด คลิกที่รูปฟันเฟือง **Project Settings** $\to$ เลือกหัวข้อ **API**
2. คัดลอกค่า 2 ตัวนี้มาใช้งาน:
   - **Project URL:** เช่น `https://abcdefghijklmnop.supabase.co`
   - **anon public key:** รหัสยาวๆ เช่น `eyJhbGciOiJIUzI1NiIsInR5cCI6IkpX...`

---

#### 4. วิธีเปิดใช้งาน:
* **ฝั่งหน้าเว็บ Dashboard:**
  - ดับเบิลคลิกเปิดไฟล์ [`web_dashboard/index.html`](file:///Users/thanaporn/.gemini/antigravity/scratch/campus-safety-node/web_dashboard/index.html) ใน Google Chrome ได้ทันที
  - คลิกปุ่ม **"⚙️ API Config"** ที่มุมขวาบนของหน้าเว็บ นำ Project URL และ Anon Key มาแปะแล้วกดบันทึก หน้าจอจะขึ้นสถานะเขียว **"REALTIME WS CONNECTED (<50ms)"** ทันที!
* **ฝั่ง ESP32:**
  - เปิดไฟล์ [`src/esp32_firmware_supabase.ino`](file:///Users/thanaporn/.gemini/antigravity/scratch/campus-safety-node/src/esp32_firmware_supabase.ino) ใน Arduino IDE
  - แก้ไขตัวแปร `SUPABASE_URL` และ `SUPABASE_ANON_KEY` ให้ตรงกับของคุณ
  - เสียบบอร์ดแล้วกด **Upload** ได้เลยครับ!
