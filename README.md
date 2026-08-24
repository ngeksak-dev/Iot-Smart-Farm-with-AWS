# Smart Farm IoT Server

Monitor sensors and control equipment on a farm, from a web page on your phone
or computer.

You plug sensors into an ESP32 board. The board sends readings over WiFi to a
server you run yourself. A web dashboard shows the readings live and lets you
switch pumps, fans and lights on and off.

**No programming is needed to follow this guide.** You copy commands into a
terminal and paste them. Every command is given in full, and after each one
there is a check so you know it worked before moving on.

---

## Before you start

You need:

| | |
|---|---|
| **A computer** | Running Ubuntu Linux. This becomes the server and must stay switched on. |
| **An ESP32 board** | The small WiFi board the sensors plug into. |
| **Sensors** | At least one — a DHT22 (temperature and humidity) is the easiest to start with. |
| **A relay module** | Optional, only if you want to switch pumps or lights. |
| **Arduino IDE** | Free software, installed on any computer, used once to load the program onto the ESP32. |
| **WiFi** | The server and the ESP32 must be on the same network. |

You do **not** need to buy anything online or pay for any service.

### How long it takes

About an hour. Most of that is the computer downloading things while you wait.

### The 12 steps

| Steps | What happens |
|---|---|
| 1–2 | Install Docker and make a folder for the project |
| 3–6 | Create four configuration files by copying commands |
| 7–9 | Start the server and check it is running |
| 10 | Load the dashboard software |
| 11 | Phone and email alerts — optional |
| 12 | Put the program on the ESP32 board |

Steps 1 to 11 are all done on the server. The ESP32 is left until last, so the
whole system is ready and waiting before you connect any hardware.

**Do the steps in order.** Later steps depend on earlier ones. If you skip
ahead, you will get errors that are hard to understand.

### A note on the commands

Lines starting with `sudo` will ask for your password. Type it and press Enter
— **the screen will not show anything as you type**, not even dots. That is
normal.

If a command produces no output at all, that usually means it worked. Errors
are noisy; success is often silent.

---

## What's in this repository

| File | What it is | When you need it |
|---|---|---|
| `README.md` | This guide | Start here |
| `flows.json` | The dashboard software | Step 10 |
| `esp32.ino` | The program for the ESP32 board | Step 11 |
| `notify_service.py` | Alert service | Step 12 (optional) |
| `ADVANCED.md` | Automatic control, charts, running on a cloud server | Later |
| `FEATURES.md` | List of everything the system does | Reference |
| `UAT_TEST_CASES.md` | Tests to confirm your install works | Reference |

Everything else in the guide is created by copying commands, so there is
nothing else to download.

## What you end up with

A dashboard at `http://YOUR-SERVER-IP:1880/dashboard` with six pages:

| Page | What it does |
|---|---|
| Overview | Live readings and on/off buttons |
| IoT Devices | Add sensors and relays |
| Trigger | Rules like "if it gets too hot, turn on the fan" |
| Schedule | Timers, like "water at 6am every day" |
| Notifications | Alerts to your phone |
| ESP32 Boards | Add and monitor boards |

Adding a sensor later is done entirely from the web page. You load the program
onto the ESP32 **once** in step 11 and never touch it again.

---

## Step 1 — Install Docker

Docker is the software that runs the five programs this system needs. Without
it nothing else will work.

Open a terminal and run these **four blocks in order**. Each one must finish
before you start the next.

**1a. Prepare the system**

```bash
sudo apt update
sudo apt install ca-certificates curl gnupg lsb-release -y
```

**1b. Tell your computer where to get Docker**

```bash
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
```

**1c. Install Docker** — this is the important one, and takes a few minutes

```bash
sudo apt update
sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin -y
```

**1d. Allow your account to use Docker**

```bash
sudo usermod -aG docker $USER
newgrp docker
```

### ✅ Check step 1 worked

```bash
docker run hello-world
```

You should see a message starting with **"Hello from Docker!"**

| If instead you see | Do this |
|---|---|
| `docker: command not found` | Block **1c** did not run. Go back and run it. |
| `permission denied` | Close the terminal, open a new one, try again. |
| `Cannot connect to the Docker daemon` | Run `sudo systemctl start docker`, then try again. |

**Do not continue until you see "Hello from Docker!".** Every later step
depends on this.

> **Only if you had Docker installed before:** an old version can conflict with
> the new one. Remove it and repeat step 1:
>
> ```bash
> sudo apt remove docker docker-engine docker.io containerd runc -y
> ```
>
> On a new computer you can ignore this.

### If you see "403 Forbidden" errors (optional)

Some people run `sudo apt upgrade` before installing Docker. It is not needed,
and on some networks it stops part-way with errors like:

```
Err:28 http://kh.archive.ubuntu.com/ubuntu resolute-updates/main amd64 linux-firmware-amd-misc
  403  Forbidden [IP: 202.79.180.254 80]
E: Unable to fetch some archives, maybe run apt update or try with --fix-missing?
```

This is a fault at the download server, not on your computer. The files that
fail are `linux-firmware-*`, which are drivers for hardware you almost
certainly do not have.

**Nothing here matters for this system.** Ignore it and carry on with step 1.

If you would rather fix it, try these in order:

```bash
# 1. Simply try again — these failures are often temporary
sudo apt update
sudo apt upgrade -y --fix-missing
```

```bash
# 2. Switch to Ubuntu's main download server
sudo cp /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources.bak
sudo sed -i 's|http://kh.archive.ubuntu.com/ubuntu|http://archive.ubuntu.com/ubuntu|g' \
  /etc/apt/sources.list.d/ubuntu.sources
sudo apt update
sudo apt upgrade -y
```

If that file does not exist, your Ubuntu is older — use `/etc/apt/sources.list`
in the two commands above instead.

```bash
# 3. Skip just the package that fails, and upgrade the rest
sudo apt-mark hold linux-firmware-amd-misc
sudo apt upgrade -y
```

Add another `apt-mark hold` line for any other package named in the error.

> Do **not** use `apt --fix-broken install` or `dpkg --configure -a` here.
> Nothing is broken — a download simply did not finish — and those commands
> will not help.

---

## Step 2 — Make a folder for the project

Everything you create lives in one folder.

```bash
mkdir -p ~/smartfarm && cd ~/smartfarm
```

### ✅ Check step 2 worked

```bash
pwd
```

It should print a path ending in `/smartfarm`.

**Stay in this folder for steps 3 to 9.** If you close the terminal, get back
with `cd ~/smartfarm` before continuing.

---
## Step 3 — Create the MQTT settings file

MQTT is how the ESP32 board talks to the server. This file tells it to require
a password.

Copy this whole block — from `mkdir` to the final `EOF` — and paste it into the
terminal:

```bash
mkdir -p mosquitto/config mosquitto/data mosquitto/log

cat > mosquitto/config/mosquitto.conf << 'EOF'
listener 1883
allow_anonymous false
password_file /mosquitto/config/passwd

listener 9001
protocol websockets
allow_anonymous false
password_file /mosquitto/config/passwd

persistence true
persistence_location /mosquitto/data/
log_dest file /mosquitto/log/mosquitto.log
EOF

touch mosquitto/config/passwd
sudo chown -R 1883:1883 mosquitto/
```

The empty `passwd` file matters. The program refuses to start without it, and
it gets a real password in step 7. The `chown` line gives the program
permission to write its own log files — skipping it is the most common reason
this container fails later.

### ✅ Check step 3 worked

```bash
ls -la mosquitto/config/
```

You should see **two** files: `mosquitto.conf` and `passwd`, both owned by
`1883`.

---

## Step 4 — Create the database file

This creates the tables that store your sensors, rules and settings.

Copy this whole block and paste it in. It is long — that is expected, and you
paste it all at once:

```bash
mkdir -p mysql/init

cat > mysql/init/01_init.sql << 'EOF'
CREATE DATABASE IF NOT EXISTS iot;
USE iot;

-- Registered ESP32 boards, identified by MAC address.
-- name stays NULL until the board is registered in the dashboard,
-- which is how it's listed as "unregistered".
CREATE TABLE IF NOT EXISTS tbl_esp32_board (
    id INT AUTO_INCREMENT PRIMARY KEY,
    mac_address VARCHAR(20) NOT NULL UNIQUE,
    name VARCHAR(100) NULL,
    location VARCHAR(100),
    status VARCHAR(20) DEFAULT 'offline',
    last_ip VARCHAR(45),
    last_seen DATETIME,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Sensors and controllers.
-- channels holds relay channels as JSON:
--   [{"name":"Pump","pin":"26","enabled":true,"active_low":false}, ...]
CREATE TABLE IF NOT EXISTS tbl_iotdevice (
    deviceid INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    device_type ENUM('sensor','controller') NOT NULL DEFAULT 'sensor',
    sensors VARCHAR(100),
    controller VARCHAR(100),
    channels TEXT,
    pin VARCHAR(20),
    board_id INT DEFAULT NULL,
    zone_id INT DEFAULT NULL,
    display_order INT NOT NULL DEFAULT 0,
    remark TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (board_id) REFERENCES tbl_esp32_board(id) ON DELETE SET NULL
);

-- User-defined controller types for the device form dropdown.
CREATE TABLE IF NOT EXISTS tbl_controller_type (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL UNIQUE
);

-- Condition -> action rules. act_pin targets one relay channel.
-- conditions is JSON describing what to watch for, e.g.
--   {"metric":"temperature","mode":"above","value":30,"for_seconds":120}
--   {"metric":"motion","mode":"clear_for","value":null,"for_seconds":600}
-- time_from/time_to limit a rule to certain hours (NULL = any time,
-- and the window may wrap past midnight).
CREATE TABLE IF NOT EXISTS tbl_trigger (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NULL,
    cond_device VARCHAR(20),
    conditions TEXT,
    act_device VARCHAR(20),
    act_controller VARCHAR(100),
    act_pin INT DEFAULT NULL,
    action VARCHAR(10) DEFAULT 'on',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    time_from TIME NULL,
    time_to TIME NULL,
    duration INT DEFAULT 300,
    duration_display VARCHAR(50),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- One-time schedules. executed=1 stops them firing twice.
CREATE TABLE IF NOT EXISTS tbl_onetime_schedule (
    id INT AUTO_INCREMENT PRIMARY KEY,
    date DATE,
    time TIME,
    action VARCHAR(10) DEFAULT 'on',
    read_sensor TINYINT(1) DEFAULT 0,
    duration INT DEFAULT 300,
    duration_display VARCHAR(50),
    deviceid VARCHAR(20),
    controller VARCHAR(100),
    pin INT DEFAULT NULL,
    executed TINYINT(1) DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Weekly schedules. day_of_week is a comma-separated list.
CREATE TABLE IF NOT EXISTS tbl_repeat_schedule (
    id INT AUTO_INCREMENT PRIMARY KEY,
    day_of_week VARCHAR(100),
    time TIME,
    action VARCHAR(10) DEFAULT 'on',
    read_sensor TINYINT(1) DEFAULT 0,
    duration INT DEFAULT 300,
    duration_display VARCHAR(50),
    deviceid VARCHAR(20),
    controller VARCHAR(100),
    pin INT DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Overview zones. A device with zone_id NULL sits in "No zone", and that
-- group only appears on the Overview page while something is in it.
CREATE TABLE IF NOT EXISTS tbl_zone (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    sort_order INT NOT NULL DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Key/value settings, currently the notification service URL and token.
-- These live in the database rather than the flow: Node-RED strips passwords
-- from an exported flow but keeps custom headers, so a token typed into an
-- HTTP node would be published in flows.json.
CREATE TABLE IF NOT EXISTS tbl_setting (
    skey VARCHAR(50) PRIMARY KEY,
    svalue TEXT
);
INSERT IGNORE INTO tbl_setting (skey, svalue) VALUES
  ('notify_url',   'http://host.docker.internal:8000'),
  ('notify_token', '');

-- Alert rules. Recipients are NOT stored here — a rule names a recipient key
-- and the notification service maps that to a chat id or address.
CREATE TABLE IF NOT EXISTS tbl_notify_rule (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100),
    kind VARCHAR(20) NOT NULL DEFAULT 'threshold',
    enabled TINYINT(1) NOT NULL DEFAULT 1,
    severity ENUM('info','warning','critical') NOT NULL DEFAULT 'warning',
    cond_device VARCHAR(20),
    metric VARCHAR(30),
    mode VARCHAR(10),
    value DOUBLE,
    value2 DOUBLE,
    for_seconds INT DEFAULT 0,
    offline_seconds INT DEFAULT 300,
    interval_seconds INT DEFAULT 21600,
    cooldown_seconds INT DEFAULT 1800,
    repeat_seconds INT DEFAULT 0,
    send_recovery TINYINT(1) NOT NULL DEFAULT 1,
    quiet_from TIME NULL,
    quiet_to TIME NULL,
    recipients TEXT,
    contact_ids TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- What the dashboard asked the service to send.
CREATE TABLE IF NOT EXISTS tbl_notify_log (
    id INT AUTO_INCREMENT PRIMARY KEY,
    rule_id INT NULL,
    rule_name VARCHAR(100),
    channel VARCHAR(20),
    target VARCHAR(200),
    severity VARCHAR(20),
    message TEXT,
    status VARCHAR(20),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Repeating interval schedules.
CREATE TABLE IF NOT EXISTS tbl_interval_schedule (
    id INT AUTO_INCREMENT PRIMARY KEY,
    `interval` INT DEFAULT 30,
    action VARCHAR(10) DEFAULT 'on',
    read_sensor TINYINT(1) DEFAULT 0,
    duration INT DEFAULT 300,
    duration_display VARCHAR(50),
    deviceid VARCHAR(20),
    controller VARCHAR(100),
    pin INT DEFAULT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
EOF
```

### ✅ Check step 4 worked

```bash
ls -la mysql/init/
```

You should see `01_init.sql` with a size of around 5000 bytes.

---

## Step 5 — Create the dashboard settings file

This sets the username and password for the dashboard editor.

```bash
mkdir -p nodered/data

cat > nodered/data/settings.js << 'EOF'
module.exports = {
    flowFile: 'flows.json',
    flowFilePretty: true,

    // Editor login. The password is a bcrypt hash of "admin123".
    // Generate your own with:
    //   docker exec -it nodered npx node-red-admin hash-pw
    adminAuth: {
        type: "credentials",
        users: [{
            username: "admin",
            password: "$2b$08$cUbwOzt2g5x/W5Y4FBr0kehuwH8lkjvchupnLZpVMDQYanK1CynnC",
            permissions: "*"
        }]
    },

    uiPort: process.env.PORT || 1880,
    logging: {
        console: { level: "info", metrics: false, audit: false }
    },
    exportGlobalContextKeys: false,
    functionGlobalContext: {},
    editorTheme: {
        projects: { enabled: false }
    }
}
EOF

sudo chown -R 1000:1000 nodered
```

The login is **admin** / **admin123**. To change it later, see the comment
inside the file.

This protects the **editor**, where the software is built. The dashboard
itself stays open, so phones and tablets can use it without logging in.

### ✅ Check step 5 worked

```bash
ls -la nodered/data/
```

You should see `settings.js`.

---

## Step 6 — Create the main configuration file

This one file describes all five programs and how they fit together.

**Before you paste it, decide your timezone.** The file below says
`Asia/Phnom_Penh`. If you are elsewhere, change all five occurrences after
pasting, or edit the block before you paste. Getting this wrong makes timers
run at the wrong hour.

```bash
cat > docker-compose.yml << 'EOF'
services:

  mqtt:
    image: eclipse-mosquitto:2
    container_name: mqtt
    restart: unless-stopped
    ports:
      - "1883:1883"
      - "9001:9001"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
    environment:
      - TZ=Asia/Phnom_Penh

  mysql:
    image: mysql:8.0
    container_name: mysql
    restart: unless-stopped
    environment:
      MYSQL_ROOT_PASSWORD: root1234
      MYSQL_DATABASE: iot
      MYSQL_USER: admin
      MYSQL_PASSWORD: admin123
      TZ: Asia/Phnom_Penh
    ports:
      - "3306:3306"
    volumes:
      - mysql_data:/var/lib/mysql
      - ./mysql/init:/docker-entrypoint-initdb.d
    command: --default-authentication-plugin=mysql_native_password

  influxdb:
    image: influxdb:2.7
    container_name: influxdb
    restart: unless-stopped
    ports:
      - "8086:8086"
    environment:
      DOCKER_INFLUXDB_INIT_MODE: setup
      DOCKER_INFLUXDB_INIT_USERNAME: admin
      DOCKER_INFLUXDB_INIT_PASSWORD: admin123
      DOCKER_INFLUXDB_INIT_ORG: iot-org
      DOCKER_INFLUXDB_INIT_BUCKET: sensor_data
      DOCKER_INFLUXDB_INIT_RETENTION: 30d
      TZ: Asia/Phnom_Penh
    volumes:
      - influxdb_data:/var/lib/influxdb2

  nodered:
    image: nodered/node-red:latest
    container_name: nodered
    restart: unless-stopped
    ports:
      - "1880:1880"
    volumes:
      - ./nodered/data:/data
    depends_on:
      - mqtt
      - mysql
      - influxdb
    # Lets Node-RED reach services running on the host. Not needed for this
    # guide, but the alert service in ADVANCED.md requires it. Leave it in:
    # adding it later recreates the container, which wipes the npm packages
    # installed in step 8.
    extra_hosts:
      - "host.docker.internal:host-gateway"
    environment:
      - TZ=Asia/Phnom_Penh

  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    restart: unless-stopped
    ports:
      - "3000:3000"
    environment:
      GF_SECURITY_ADMIN_USER: admin
      GF_SECURITY_ADMIN_PASSWORD: admin123
      TZ: Asia/Phnom_Penh
    volumes:
      - grafana_data:/var/lib/grafana
    depends_on:
      - influxdb

volumes:
  mysql_data:
  influxdb_data:
  grafana_data:
EOF
```

### ✅ Check step 6 worked

```bash
ls -la docker-compose.yml
```

The file should exist and be around 2000 bytes.

---
## Step 7 — Start the server

```bash
docker compose up -d
```

The first run downloads about 1 GB and takes several minutes. You will see
progress bars. Wait until you are back at the prompt.

**Now wait another 30 seconds** — the database needs time to set itself up the
first time.

### ✅ Check the five programs are running

```bash
docker ps
```

You should see five lines, one each for `mqtt`, `mysql`, `influxdb`, `nodered`
and `grafana`, all saying **`Up`**.

| If you see | Do this |
|---|---|
| A container says `Restarting` | Run `docker logs NAME --tail 20` to see why. For `mqtt`, it is almost always the ownership step in step 3 — run `sudo chown -R 1883:1883 ~/smartfarm/mosquitto/` then `docker restart mqtt`. |
| `port is already allocated` | Something else on the computer is using that port. Stop it, or change the port number in `docker-compose.yml`. |
| Fewer than five containers | Run `docker compose up -d` again and read the error. |

### Set the MQTT password

The board and the dashboard both log in with this.

```bash
docker exec mqtt mosquitto_passwd -b /mosquitto/config/passwd mqtt mqtt123
docker exec mqtt chmod 0700 /mosquitto/config/passwd
docker restart mqtt
```

Use `-b`, not `-c`. The `-c` option erases the file and starts over.

### ✅ Check the password works

This command should **fail** — that is the correct result, because it proves
anonymous access is blocked:

```bash
docker run --rm --network container:mqtt eclipse-mosquitto:2 \
  mosquitto_pub -h localhost -t esp32/control -m "test"
```

You want to see `Connection Refused: not authorised`.

---

## Step 8 — Install the dashboard add-ons

```bash
docker exec -it -w /data nodered npm install \
  @flowfuse/node-red-dashboard \
  node-red-node-mysql \
  node-red-contrib-influxdb

docker restart nodered
```

This takes a couple of minutes and prints a lot of text. Warnings are normal;
errors are not.

> **The `-w /data` part is essential.** Without it the add-ons are installed in
> a place that gets wiped whenever the container is rebuilt, and the dashboard
> then fails with "Flows stopped due to missing node types".

### ✅ Check step 8 worked

Wait 30 seconds after the restart, then:

```bash
docker logs nodered --tail 15
```

You want to see **`Server now running at http://127.0.0.1:1880/`** and no
mention of missing node types.

---

## Step 9 — Check everything before continuing

**Are the database tables there?**

```bash
docker exec -it mysql mysql -u admin -padmin123 iot -e "SHOW TABLES;"
```

You should see 11 table names, all starting with `tbl_`.

If you see none, step 4 did not run properly. Fix it by deleting the database
and starting it again — this is safe now because there is no data yet:

```bash
cd ~/smartfarm
docker compose down -v
docker compose up -d
```

**Do the clocks agree?**

```bash
date
docker exec nodered date
```

Both should show your local time. If they differ, the timezone in step 6 is
wrong — fix `docker-compose.yml` and run `docker compose up -d` again.

**What is this computer's address?**

```bash
hostname -I
```

Write down the first number, e.g. `192.168.1.126`. You need it twice more.
Wherever this guide says `YOUR-SERVER-IP`, use that number.

---

## Step 10 — Load the dashboard software

1. On any computer on the same network, open a browser and go to
   `http://YOUR-SERVER-IP:1880`
2. Log in with **admin** / **admin123**
3. Click the **☰ menu** at the top right → **Import**
4. Click **select a file to import**, choose `flows.json` from this repository
5. Click **Import**

**Now the part people miss.** For security, passwords are stripped out of the
file, so you must type them back in before it will work:

6. In the right-hand panel, click the dropdown that says *Information* and
   choose **Configuration nodes**
7. Double-click **`mqtt-server`** → click the **Security** tab →
   Username `mqtt`, Password `mqtt123` → **Update**
8. Double-click **`iotdb`** → Username `admin`, Password `admin123` → **Update**
9. Click the red **Deploy** button at the top right

### ✅ Check step 10 worked

Look at the nodes on the screen. Under the ones labelled `esp32/...` you should
see a small green square and the word **connected**.

If it says *disconnected*, the MQTT password in point 7 is wrong or was not
saved.

> Skipping points 6 to 8 fails **silently**. There is no error — the dashboard
> simply never shows any data. If nothing works later, come back and check
> this first.

---

## Step 11 — Alerts on your phone (optional)

Adds Telegram and email alerts.

**This step is optional, and it does not need the ESP32.** Setting it up now
means alerts are ready the moment the board starts reporting in step 12. If
you would rather see sensor readings first, skip ahead to step 12 and come
back to this later — nothing is lost either way.

The service runs directly on your machine, not in Docker. Recipients are
configured in its own file rather than in the dashboard, so chat IDs and email
addresses never end up in the database or in an exported flow.

**You already have `notify_service.py`. Nothing in it needs editing** — every
setting is read from an environment file created in step 11.4.

### 11.1 Install the service

Do this first — it is the slowest part, and it needs nothing from Telegram or
email.

```bash
sudo apt update && sudo apt install python3-venv -y

mkdir -p ~/notify && cd ~/notify
```

Copy `notify_service.py` into `~/notify/`, then:

```bash
cd ~/notify
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install "fastapi[standard]" aiomqtt aiosmtplib httpx
deactivate
```

**Check:**

```bash
~/notify/.venv/bin/python -c "import fastapi, aiomqtt, aiosmtplib, httpx; print('imports OK')"
```

If this fails, `pip install` ran outside the virtual environment. Repeat from
`source .venv/bin/activate`.

---

### 11.2 Telegram bot

1. Open Telegram, search **@BotFather**, send `/newbot`
2. Give it a display name, then a username ending in `bot`
3. Copy the token it returns — looks like `7891234567:AAG3xKp9-mExample`
4. **Search for your new bot and send it any message.** A bot cannot start a
   conversation, so until you do this every send fails with "chat not found"
5. Get your chat ID:

```bash
curl -s "https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates"
```

Look for `"chat":{"id":123456789` in the reply. That number is your chat ID.

Nothing in the reply means you skipped step 4, or the token is wrong.

**For a group instead:** add the bot to the group, post a message there, then
run the same command. Group IDs are negative, like `-4001234567` — keep the
minus sign.

#### Check it works before going further

```bash
curl -s -X POST "https://api.telegram.org/bot<YOUR_TOKEN>/sendMessage" \
  -d "chat_id=<YOUR_CHAT_ID>" -d "text=test"
```

You want `{"ok":true,...}` **and** the message on your phone. If this fails,
nothing built on top of it can work.

---

### 11.3 Email (skip if you only want Telegram)

For Gmail you need an **App Password** — your normal password will be refused.

1. Turn on 2-Step Verification at https://myaccount.google.com/security
2. Go to https://myaccount.google.com/apppasswords
3. Create one named "Smart Farm"
4. Copy the 16 characters, e.g. `abcd efgh ijkl mnop`

Other providers work with normal SMTP credentials — port 465 for SSL, 587 for
STARTTLS.

---

### 11.4 Configuration

This is the only file you edit. It holds your bot token and email password, so
it is readable by root only.

```bash
sudo nano /etc/smartfarm-notify.env
```

Paste this and change every line marked `<<<`:

```ini
# Password for your own API. Generate with:  openssl rand -hex 32
API_SECURITY_TOKEN=paste-a-long-random-string-here          # <<<

# From BotFather, step 11.2
TELEGRAM_TOKEN=7891234567:AAG3xKp9-mExample                 # <<<

# Email — leave these as-is if you skipped step 2
SMTP_HOST=smtp.gmail.com
SMTP_PORT=465
EMAIL_FROM=you@gmail.com                                    # <<<
SMTP_PASS=abcd efgh ijkl mnop                               # <<<

# Who can be notified. ONE LINE, valid JSON.
# The dashboard only ever sees the "label", never the id or address.
RECIPIENTS={"me":{"label":"My phone","telegram":"123456789"}}   # <<<

# The service can also watch MQTT topics. Not needed for alerts.
ENABLE_MQTT=0
```

Generate the API token with:

```bash
openssl rand -hex 32
```

Lock the file down:

```bash
sudo chmod 600 /etc/smartfarm-notify.env
```

#### Recipients

Each entry can have `telegram`, `email`, or both. A message goes to every
channel that entry defines. Three examples on one line:

```ini
RECIPIENTS={"me":{"label":"My phone","telegram":"123456789"},"team":{"label":"Farm team","telegram":"-4001234567","email":"team@example.com"},"reports":{"label":"Email only","email":"me@gmail.com"}}
```

Check it is valid JSON before continuing:

```bash
sudo grep RECIPIENTS /etc/smartfarm-notify.env | cut -d= -f2- | python3 -m json.tool
```

To add someone later, edit this file and restart the service (step 5). The
dashboard picks up the new name on its own.

> **Watch for overlap.** If one recipient has both Telegram and email, and
> another has just Telegram to the same chat, selecting both in a rule sends
> the same alert twice.

---

### 11.5 Run it as a service

```bash
whoami
```

Note that name — you need it three times below.

```bash
sudo nano /etc/systemd/system/fastapi-notifier.service
```

```ini
[Unit]
Description=Smart Farm Notification Service
After=network.target

[Service]
User=YOUR_USERNAME
WorkingDirectory=/home/YOUR_USERNAME/notify
EnvironmentFile=/etc/smartfarm-notify.env
ExecStart=/home/YOUR_USERNAME/notify/.venv/bin/python -m uvicorn notify_service:app --host 0.0.0.0 --port 8000
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Replace **`YOUR_USERNAME`** in all three places, then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now fastapi-notifier
sudo systemctl status fastapi-notifier
```

You want **`active (running)`** in green.

Keep the log open in a second terminal while testing — send failures appear
here, not in the reply:

```bash
sudo journalctl -u fastapi-notifier -f
```

---

### 11.6 Test it

```bash
TOKEN=$(sudo grep API_SECURITY_TOKEN /etc/smartfarm-notify.env | cut -d= -f2)
```

#### What loaded?

```bash
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8000/notify/health
```

Expected:

```json
{"status":"ok","recipients":1,"telegram_configured":true,"email_configured":true}
```

| Result | Meaning |
|---|---|
| `recipients:0` | `RECIPIENTS` is missing or not valid JSON |
| `telegram_configured:false` | `TELEGRAM_TOKEN` not set |
| `email_configured:false` | `EMAIL_FROM` or `SMTP_PASS` not set |
| nothing at all | service not running — see step 11.5 |
| `Invalid or missing API token` | `$TOKEN` doesn't match the file |

#### Who can it reach?

```bash
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8000/notify/recipients
```

#### Send for real

```bash
curl -X POST http://localhost:8000/notify/test \
  -H "Authorization: Bearer $TOKEN" -F "recipient=me"
```

Expected — **and a message arriving**:

```json
{"status":"sent","recipient":"me","results":{"telegram":"ok"}}
```

The service waits for Telegram before replying, so `sent` here means Telegram
really accepted it. Failures come back with the reason:

| Error | Fix |
|---|---|
| `chat not found` | You didn't message the bot first, or the chat ID is wrong |
| `Unauthorized` | Bot token is wrong |
| `Unknown recipient` | The key doesn't match `RECIPIENTS` |
| `Username and Password not accepted` | Use a Gmail App Password, not your login password |

Email lands in Spam surprisingly often on first send — check there.

---

### 11.7 Connect Node-RED

Node-RED runs in Docker, so `localhost` **inside** the container means the
container, not your machine. It reaches the host by a special name.

Confirm that name exists:

```bash
docker inspect nodered --format '{{json .HostConfig.ExtraHosts}}'
```

You want `["host.docker.internal:host-gateway"]`. If you get `[]` or `null`,
add this to the `nodered` service in `~/smartfarm/docker-compose.yml`:

```yaml
    extra_hosts:
      - "host.docker.internal:host-gateway"
```

then:

```bash
cd ~/smartfarm && docker compose up -d
```

> Recreating the container clears npm packages installed inside it. If
> Node-RED then reports "Flows stopped due to missing node types", reinstall
> them with the `-w /data` flag from step 8 above.

Now test from inside the container:

```bash
docker exec nodered node -e "fetch('http://host.docker.internal:8000/notify/health',{headers:{Authorization:'Bearer YOUR_TOKEN'}}).then(r=>r.text()).then(console.log).catch(e=>console.log('FAIL',e.message))"
```

Replace `YOUR_TOKEN` with your real token. You want the same JSON as step 11.6.

---

### 11.8 Configure the dashboard

1. Open `http://<SERVER_IP>:1880/dashboard` → **Notifications**
2. Scroll to **Notification Service** at the bottom:
   - **Service URL:** `http://host.docker.internal:8000`
   - **API token:** your `API_SECURITY_TOKEN`
3. Press **Save settings**
4. Wait about 20 seconds, then reload

Your recipient names appear as checkboxes, each with a **Test** button. Press
one — the message should arrive, same as step 11.6.

The page also shows a connection status line. If something is wrong it says
which: service unreachable, token rejected, or connected but no recipients.

##### ✅ Check step 11 worked

Press the **Test** button next to a recipient name. The message should arrive
on your phone or in your inbox within a few seconds.

If it does, alerts are fully working and will fire automatically once the board
is reporting.

#### Turn on your first alert

The **Quick Alerts** panel has one-click switches. Turn on:

**When a board goes offline**

It has nothing to watch yet — you have no board until step 12 — but it will
start working the moment one is registered.

This is the alert worth having above all others. A board that loses power or
WiFi simply goes quiet, and a quiet board never crosses a temperature
threshold, so no ordinary rule can detect it. Only this one can.

Once step 12 is done you can test it properly: unplug the ESP32 and wait five
minutes.

---

## Alert types

| Kind | Fires when |
|---|---|
| Reading too high / too low | a sensor crosses a threshold |
| Board stops reporting | a board goes silent |
| A trigger fires | an automation rule ran |
| A schedule runs | a scheduled action ran |
| Regular update | current readings every N minutes or hours |

Under **More options** on each rule:

| Field | Effect |
|---|---|
| Back to normal at | recovery point, set inside the safe range |
| Only if it stays that way for | ignores brief spikes |
| Wait at least this long between messages | minimum gap per rule |
| Remind me while it's still wrong | repeat every N hours |
| Urgent | ignores quiet hours |
| Quiet hours | holds non-urgent alerts |

The recovery point matters more than it looks. Alert above 30 and recover
below 30, and a reading sitting on 30 will alarm and clear repeatedly. Set
recovery a little inside the safe range — alert above 30, clear below 28.

---

## Step 12 — Put the program on the ESP32

This is the last piece. Done once — after this, sensors are added from the web
page and the board is never touched again.

1. Open `esp32.ino` in the Arduino IDE
2. In **Tools → Manage Libraries**, search for and install:
   - **PubSubClient** by Nick O'Leary
   - **ArduinoJson** by Benoit Blanchon
   - **DHT sensor library** by Adafruit
   - **Adafruit Unified Sensor**
3. Near the top of the file, change these four lines:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST     = "YOUR-SERVER-IP";   // from step 9
const char* MQTT_USER     = "mqtt";
```

4. Connect the ESP32 by USB
5. **Tools → Board** → *ESP32 Dev Module*
6. **Tools → Port** → pick the port that appears when you plug the board in
7. Click **Upload** (the arrow button)

### ✅ Check step 11 worked

Open **Tools → Serial Monitor** and set the speed to **115200**. Press the
board's reset button. You should see:

```
Connecting to WiFi: YourNetwork
WiFi connected, IP: 192.168.1.xxx
MQTT connected
```

| If you see | Do this |
|---|---|
| Nothing at all | Check the speed is 115200, and that the correct port is selected. |
| Dots forever after "Connecting to WiFi" | WiFi name or password is wrong. ESP32 only works on 2.4GHz networks, not 5GHz. |
| `MQTT connect failed, rc=5` | Wrong MQTT username or password. |
| `MQTT connect failed, rc=-2` | Wrong server IP, or the server is not reachable. |

---

## First use — add your first sensor

Open **`http://YOUR-SERVER-IP:1880/dashboard`** on your phone or computer.

**1. Register the board.** Go to **ESP32 Boards**. Within 30 seconds your board
appears under *Unregistered*. Type a name, for example "Greenhouse", and press
**Register**.

**2. Add a sensor.** Go to **IoT Devices**:
- Device name: anything, e.g. "Greenhouse DHT"
- ESP32 Board: pick the board you just registered
- Module: **DHT22**
- Pin: the GPIO number the sensor's data wire is connected to, e.g. `15`
- Press **Add Device**

**3. Look at it.** Go to **Overview**. Within a few seconds you should see two
boxes — Temperature and Humidity — with live numbers that update on their own.

🎉 **That is the system working.**

If you did step 11, your alerts are already active and watching this sensor.

**4. Add a relay** (only if you have one). On **IoT Devices**, choose Module
**Relay Module**, set the number of channels, then give each channel a name and
a pin. Leave **Active LOW** ticked unless the relay turns out to work
backwards. The on/off buttons appear on Overview.

### If Overview shows nothing

| Symptom | Cause |
|---|---|
| "No sensors registered yet" | The device was added without choosing a board. Edit it and pick one. |
| Boxes appear but say "waiting…" | The board is not sending. Check the Serial Monitor from step 11. |
| Everything is grey | The board stopped reporting. Check it has power and WiFi. |

You can also watch the raw data arriving:

```bash
docker exec mqtt mosquitto_sub -h localhost -u mqtt -P mqtt123 -t esp32/read_all -v
```

If numbers appear here but not on the dashboard, the problem is step 10's
passwords. If nothing appears here either, the problem is the board.

---
## Passwords used in this guide

| What | Address | Username | Password |
|---|---|---|---|
| Dashboard editor | `http://YOUR-SERVER-IP:1880` | admin | admin123 |
| Dashboard | `http://YOUR-SERVER-IP:1880/dashboard` | — | no login |
| Grafana charts | `http://YOUR-SERVER-IP:3000` | admin | admin123 |
| Database | `YOUR-SERVER-IP:3306` | admin | admin123 |
| InfluxDB | `http://YOUR-SERVER-IP:8086` | admin | admin123 |
| MQTT | `YOUR-SERVER-IP:1883` | mqtt | mqtt123 |
| Alert service | `http://YOUR-SERVER-IP:8000` | — | your API token |

> These are the defaults from this guide, and everyone reading it knows them.
> They are fine on a home or farm network that outsiders cannot reach. Change
> them before putting this on the internet, and never commit real passwords to
> a public repository.

## MQTT topics

| Topic | Direction | Payload |
|---|---|---|
| `esp32/announce` | board → server | `{"mac":"...","ip":"..."}`, every 30s |
| `esp32/status/<mac>` | board → server | `online` / `offline` (retained, last will). Published by the board but not currently consumed — offline detection uses the timing of `esp32/announce` instead, which also catches a board that loses power without a clean disconnect. |
| `esp32/config/<mac>` | server → board | retained array of sensors and relay channels |
| `esp32/read_all` | board → server | readings, tagged per pin |
| `esp32/control` | server → board | `{"pin":26,"state":"ON"}` |

## Troubleshooting

**`mqtt` container restarts in a loop.** Mosquitto runs as UID 1883 and can't write its log or read its password file:

```bash
sudo chown -R 1883:1883 ~/smartfarm/mosquitto/
docker restart mqtt
docker logs mqtt --tail 20
```

**"Flows stopped due to missing node types".** The extra nodes were installed
inside the container instead of in `/data`, and the container has since been
recreated. Reinstall with the `-w /data` flag:

```bash
docker exec -it -w /data nodered npm install \
  @flowfuse/node-red-dashboard node-red-node-mysql node-red-contrib-influxdb
docker restart nodered
```

Nothing is lost — the flow and its data are intact; Node-RED just can't load
the node types it needs.

**Flow imported and deployed, but no data anywhere and no errors.** Credentials were stripped on export — see step 10.

**Schedules fire at the wrong time.** Compare `date`, `docker exec mysql date`, and `docker exec nodered date`. If they disagree, fix `TZ` in `docker-compose.yml` and run `docker compose up -d`.

**Relay is inverted — ON switches it off.** The Active LOW setting doesn't match the board. Flip the checkbox on the device and save. Some boards wire their indicator LED across the optocoupler input, so the LED can read backwards; trust the relay's click over the LED.

**Relay is permanently on.** On an active-LOW board powered at 5V, the ESP32's 3.3V "HIGH" may not be enough to switch the optocoupler off. If the board has a **JD-VCC jumper**, remove it, connect `VCC` to 3.3V, `JD-VCC` to 5V, and share GND.

**Schema changes don't take effect.** `01_init.sql` only runs when the MySQL volume is first created. On an existing install, apply changes with `ALTER TABLE`, or destroy the volume with `docker compose down -v` — **this deletes all data**.

**`Unknown column 'enabled'` (or `pin`, `act_pin`, `time_from`) in the debug panel.** The flow was imported before the database was migrated. Run the migration in *Upgrading* below, then Deploy again — always database first, flow second.

**A trigger never fires.** Check the sensor is linked to a board (readings are matched by board MAC *and* pin, so an unlinked device has no data to test), and that the rule shows as *active* rather than *paused* in the trigger list. If it fired once and won't fire again, its condition hasn't gone false since — that's the latch described under *Automation*.

---

---

### Notification problems

**Service keeps restarting.**

```bash
sudo journalctl -u fastapi-notifier -n 50 --no-pager
```

Usually one of: the `ExecStart` path doesn't match the venv (`ls ~/notify/.venv/bin/python`),
`User=` isn't the account that owns `~/notify`, or `ModuleNotFoundError` from
installing outside the venv.

**`recipients: 0` in /health.** `RECIPIENTS` isn't valid JSON. Common causes:
smart quotes from copy-paste, a trailing comma, or the value split across
lines. Check with the `python3 -m json.tool` command in step 4.

**Dashboard shows no recipients.** Confirm the URL and token are saved on the
Notifications page, then re-run the step 7 test from inside the container. The
dashboard can only see what the container can reach.

**Nothing arrives but the log says sent.** Watch `sudo journalctl -u fastapi-notifier -f`
while triggering a test. For email, check Spam.

**Getting two of every alert.** Two selected recipients reach the same place.
Untick one.

---




## Commands worth keeping

```bash
# after editing the env file
sudo systemctl restart fastapi-notifier

# live log
sudo journalctl -u fastapi-notifier -f

# is it configured?
TOKEN=$(sudo grep API_SECURITY_TOKEN /etc/smartfarm-notify.env | cut -d= -f2)
curl -s -H "Authorization: Bearer $TOKEN" http://localhost:8000/notify/health

# quick send
curl -X POST http://localhost:8000/notify/test -H "Authorization: Bearer $TOKEN" -F "recipient=me"

# validate RECIPIENTS
sudo grep RECIPIENTS /etc/smartfarm-notify.env | cut -d= -f2- | python3 -m json.tool
```


## Next steps

You now have a system that shows live readings and switches equipment by hand,
and can send you alerts.

**[ADVANCED.md](ADVANCED.md)** covers the rest:

- **Triggers** — "if the soil is dry, run the pump for 5 minutes"
- **Schedules** — "water at 6am and 6pm every day"
- **Zones** — group devices by area on the Overview page
- **Grafana** — graphs of readings over days and weeks
- **Static IP** — stop the server's address changing after a power cut
- **Cloud and TLS** — running on a rented server, with encryption

The pages for triggers and schedules already exist and work — the advanced
guide explains how to use them well.

To confirm your installation properly, `UAT_TEST_CASES.md` has a short list of
nine checks at the top that take about twenty minutes.
