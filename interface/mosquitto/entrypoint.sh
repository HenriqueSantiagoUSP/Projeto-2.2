#!/bin/sh
# Writes the broker's password file from the environment (.env), then starts Mosquitto.
# Regenerated on every start, so changing a password in .env only needs a restart.
set -eu

PASSWD=/mosquitto/data/passwd

mosquitto_passwd -c -b "$PASSWD" hub "$MQTT_HUB_PASSWORD"
mosquitto_passwd -b "$PASSWD" pulseira "$MQTT_PULSEIRA_PASSWORD"
mosquitto_passwd -b "$PASSWD" telegraf "$MQTT_TELEGRAF_PASSWORD"
mosquitto_passwd -b "$PASSWD" notificador "$MQTT_NOTIFICADOR_PASSWORD"
mosquitto_passwd -b "$PASSWD" painel "$MQTT_PAINEL_PASSWORD"

# The broker drops to the mosquitto user before reading the file.
chown mosquitto:mosquitto "$PASSWD"
chmod 0700 "$PASSWD"

exec /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf
