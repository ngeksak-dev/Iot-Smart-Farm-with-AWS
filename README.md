## Why I built this

Small farms in Cambodia mostly run on guesswork and routine. You water because
it's morning, not because the soil is dry. You find out a pump failed when the
crop shows it, days later.

Commercial systems exist, but they don't fit. They cost more than the crop they
protect, they're sold as sealed boxes you cannot extend, and most stop working
the moment the internet does. A monitoring system that depends on someone
else's cloud is not much use in a field with intermittent signal.

The bigger problem is that almost every DIY alternative hard-codes which sensor
sits on which pin. Adding a soil probe means editing the firmware, connecting a
laptop, and re-flashing the board. That is fine for the person who wrote it and
useless for anyone else — the farmer becomes dependent on a programmer for
every small change.

So the goal was a system where **the person using it can extend it**. The board
holds no sensor configuration at all. It asks the server what is attached, and
the server tells it over MQTT. Adding a sensor is three fields on a web page.
The board is flashed once and never touched again — it even takes its WiFi
details from a page on your phone.

Everything runs on one machine you own. No account, no subscription, no data
leaving your network. If the internet drops, the pumps still run on schedule
and the triggers still fire, because none of the logic lives somewhere else.

Built as a thesis project, but built to actually be used.