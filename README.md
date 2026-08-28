# 🌱 IoT Smart Farm

### A Low-Cost IoT-Based Smart Farming Monitoring and Control System

---

## 📌 About the Project

**IoT Smart Farm** is an Internet of Things (IoT) project designed to help **home growers and small-scale farmers monitor and manage their plants more easily**.

Growing plants requires regular attention. Users need to monitor conditions such as soil moisture, temperature, humidity, and light, while also performing tasks such as watering. For people who are busy, inexperienced, or simply forget to check their plants regularly, maintaining suitable growing conditions can be difficult.

This project explores how **IoT technology can be used to monitor environmental conditions and automate basic farming tasks**.

The system uses **ESP32 devices** connected to sensors and actuators to collect information from the growing environment and perform actions based on configured conditions.

---

## 🎯 Purpose of the Project

The purpose of this project is to develop a **low-cost and configurable smart farming system** that can monitor environmental conditions and automate basic farming activities.

The system is designed to allow users to:

* 🌱 Monitor soil moisture
* 🌡️ Monitor temperature and humidity
* ☀️ Monitor light conditions
* 💧 Control water pumps and other devices
* ⚙️ Configure automatic actions using triggers
* 📅 Schedule automatic actions
* 🔔 Receive notifications about important events
* 📊 View current and historical sensor data
* 📱 Manage multiple IoT devices from a centralized system

---

## 🏠 Intended Users

The project is mainly intended for:

* 🏡 Home growers
* 🌾 Small-scale farmers
* 🌱 Beginners interested in smart farming
* 🎓 Students and researchers
* 💻 People interested in IoT and automation

The goal is to provide a system that can be used on a **small scale without requiring advanced knowledge of IoT programming**.

---

## 🧠 How the System Works

The basic concept is:

```text
┌───────────────┐
│    Sensors    │
└───────┬───────┘
        │
        ▼
┌───────────────┐
│     ESP32     │
└───────┬───────┘
        │
       MQTT
        │
        ▼
┌───────────────┐
│   Node-RED    │
└───────┬───────┘
        │
        ├──────────────────┬──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│     MySQL     │  │    InfluxDB   │  │  Automation   │
│               │  │               │  │               │
│ Device        │  │ Sensor        │  │ Triggers &    │
│ Information   │  │ History       │  │ Schedules     │
└───────────────┘  └───────────────┘  └───────────────┘
        │                  │                  │
        └──────────────────┴──────────────────┘
                           │
                           ▼
                   ┌───────────────┐
                   │    Grafana    │
                   │   Monitoring  │
                   └───────────────┘
```

### 💧 Example: Automatic Watering

For example, when the soil becomes too dry:

```text
Soil Moisture
      │
      ▼
Below Configured Threshold
      │
      ▼
Trigger Activated
      │
      ▼
Water Pump ON
      │
      ▼
Soil Moisture Improves
      │
      ▼
Water Pump OFF
```

This allows repetitive tasks to be automated while users can still **monitor and control their system**.

---

## 🛠️ Technology Stack

| Component               | Technology |
| ----------------------- | ---------- |
| IoT Device              | ESP32      |
| Communication           | MQTT       |
| Middleware & Automation | Node-RED   |
| Device Information      | MySQL      |
| Sensor Data             | InfluxDB   |
| Visualization           | Grafana    |
| Cloud Deployment        | AWS        |

---

## 🚀 Setup Guide

The complete installation and configuration process is available in the online setup guide.

👉 [**Open the Smart Farm Setup Guide**](https://ngeksak-dev.github.io/Iot-Smart-Farm-with-AWS/)

The guide explains how to set up the system components and configure the Smart Farm project.

---

## 📚 Project Context

This project is developed as an **academic/thesis project** to explore the practical use of IoT technology for small-scale smart farming.

The project focuses on combining:

**Monitoring + Device Control + Automation + Data Visualization**

into a single IoT platform.

---

## 🌱 IoT Smart Farm

**Making plant monitoring and basic farming automation simpler through IoT.**
