/*
 * Copyright (c) 2026 Igalia S.L.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include "peer.h"

LOG_MODULE_REGISTER(libpeer_test, CONFIG_LIBPEER_SAMPLE_LOG_LEVEL);

#define WEBRTC_SIGNALING_SERVER "mqtts://broker.emqx.io/public/"
#define WEBRTC_TEST_WEB "https://sepfy.github.io/libpeer/?id="

PeerConnection* peer_conn;

static void iceconn_statechange_cb(PeerConnectionState state, void *user_data)
{
	LOG_INF("Connection state: %d", state);
}

int msg_idx = 0;

static void message_cb(char *msg, size_t len, void *userdata, uint16_t sid)
{
	if (strcmp(msg, "ping") == 0) {
		peer_connection_datachannel_send_sid(peer_conn, "pong",
						strlen("pong"), sid);
	} else {
		peer_connection_datachannel_send_sid(peer_conn, msg, len, sid);
	}
}

static void open_cb(void *userdata)
{
	LOG_DBG("Data channel open");
}

static void close_cb(void *userdata)
{
	LOG_DBG("Data channel closed");
}


#define THREAD_PRIORITY  5
#define SIGNALING_STACK  4096
#define CONNECTION_STACK  8192
K_THREAD_STACK_DEFINE(signaling_stack, SIGNALING_STACK);
static struct k_thread signaling_thread;
K_THREAD_STACK_DEFINE(connection_stack, CONNECTION_STACK);
static struct k_thread connection_thread;

extern int peer_signaling_loop(void);

static void signaling_thread_fn(void *p1, void *p2, void *p3)
{
	while (true) {
		if (peer_signaling_loop()) {
			k_msleep(10);
		}
	}
}

static void connection_thread_fn(void *p1, void *p2, void *p3)
{
	while (true) {
		peer_connection_loop(peer_conn);
		k_msleep(1);
	}
}


#define SIGNALING_ENDPOINT_MAX_LEN 256
static int libpeer_test(const struct shell *sh, size_t argc, char **argv)
{
	PeerConfiguration config = {
		.ice_servers = {
			{.urls = "stun:stun.l.google.com:19302"}},
		.audio_codec = CODEC_NONE,
		.video_codec = CODEC_NONE,
		.datachannel = DATA_CHANNEL_STRING,
	};
	char signaling_endpoint[SIGNALING_ENDPOINT_MAX_LEN] = {'\0'};

	peer_init();
	peer_conn = peer_connection_create(&config);
	if (!peer_conn) {
		LOG_ERR("peer_connection_create() failed");
		return 1;
	}
	peer_connection_oniceconnectionstatechange(peer_conn,
				iceconn_statechange_cb);
	peer_connection_ondatachannel(peer_conn, message_cb,
				open_cb, close_cb);
	snprintf(signaling_endpoint, sizeof(signaling_endpoint),
				WEBRTC_SIGNALING_SERVER "%s", argv[1]);
	LOG_INF("Open %s%s in a browser and click 'Connect'",
		WEBRTC_TEST_WEB, argv[1]);
	peer_signaling_connect(signaling_endpoint, "", peer_conn);

	k_thread_create(&signaling_thread, signaling_stack,
			K_THREAD_STACK_SIZEOF(signaling_stack),
			signaling_thread_fn, NULL, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&signaling_thread, "signaling");
	k_thread_create(&connection_thread, connection_stack,
			K_THREAD_STACK_SIZEOF(connection_stack),
			connection_thread_fn, NULL, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&connection_thread, "peer_connection");

	return 0;
}

SHELL_CMD_ARG_REGISTER(libpeer_test, NULL,
			"libpeer test <conn_id>\r\n"
			"Starts a WebRTC datachannel libpeer test",
			libpeer_test, 2, 0);

int main(void)
{
	return 0;
}
