/*
 * webs.c
 *
 * Copyright (C) 2005 - 2018 Sebastian Gottschall <sebastian.gottschall@newmedia-net.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id:
 */

#define VALIDSOURCE 1

#ifdef WEBS
#include <webs.h>
#include <uemf.h>
#include <ej.h>
#else				/* !WEBS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <httpd.h>
#include <errno.h>
#endif				/* WEBS */

#include <proto/ethernet.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/klog.h>
#include <sys/wait.h>
#include <dd_defs.h>
#include <cy_conf.h>
// #ifdef EZC_SUPPORT
#include <ezc.h>
// #endif
#include <broadcom.h>
#include <wlutils.h>
#include <netdb.h>
#include <utils.h>
#include <stdarg.h>
#include <sha1.h>
#ifdef HAVE_SAMBA_SERVER
#include <jansson.h>
#endif

extern int get_merge_ipaddr(webs_t wp, char *name, char *ipaddr);

void wan_proto(webs_t wp)
{
	char *enable;

	enable = websGetVar(wp, "wan_proto", NULL);
	nvram_set("wan_proto", enable);
}

#ifdef FILTER_DEBUG
extern FILE *debout;

#define D(a) fprintf(debout,"%s\n",a); fflush(debout);
#else
#define D(a)
#endif

void dhcpfwd(webs_t wp)
{
	int enable;

	enable = websGetVari(wp, "dhcpfwd_enable", 0);
	if (enable)
		nvram_set("lan_proto", "static");
	nvram_seti("dhcpfwd_enable", enable);

}

#ifdef HAVE_CCONTROL

void execute(webs_t wp);

{
	char *var = websGetVar(wp, "command", "");

	FILE *fp = popen(var, "rb");
	if (fp) {
		FILE *out = fopen("/tmp/.result", "wb");
		while (!feof(fp))
			putc(getc(fp), out);
		fclose(out);
		pclose(fp);
	}
}

#endif
void clone_mac(webs_t wp)
{
	wp->p->clone_wan_mac = 1;
}

/*
 * Delete lease 
 */
void delete_leases(webs_t wp)
{
	char *iface;
	char *ip;
	char *mac;

	if (nvram_match("lan_proto", "static"))
		return;

	if (nvram_matchi("fon_enable", 1)
	    || (nvram_matchi("chilli_nowifibridge", 1)
		&& nvram_matchi("chilli_enable", 1))) {
		iface = nvram_safe_get("wl0_ifname");
	} else {
		if (nvram_matchi("chilli_enable", 1))
			iface = nvram_safe_get("wl0_ifname");
		else
			iface = nvram_safe_get("lan_ifname");
	}
	//todo. detect correct interface

	ip = websGetVar(wp, "ip_del", NULL);
	mac = websGetVar(wp, "mac_del", NULL);

	eval("dhcp_release", iface, ip, mac);
}

#if defined(HAVE_PPTPD) || defined(HAVE_PPPOESERVER)
void delete_pptp(webs_t wp)
{
	int iface = websGetVari(wp, "if_del", 0);
	if (iface)
		kill(iface, SIGTERM);
}
#endif
void save_wifi(webs_t wp)
{
	// fprintf (stderr, "save wifi\n");
	char *var = websGetVar(wp, "wifi_display", NULL);

	if (var) {
		if (has_ad(var))
			nvram_set("wifi_display", "giwifi0");
		else
			nvram_set("wifi_display", var);

	}
}

void dhcp_renew(webs_t wp)
{
	killall("igmprt", SIGTERM);
	killall("udhcpc", SIGUSR1);
}

void dhcp_release(webs_t wp)
{

	killall("igmprt", SIGTERM);
	nvram_set("wan_ipaddr", "0.0.0.0");
	nvram_set("wan_netmask", "0.0.0.0");
	nvram_set("wan_gateway", "0.0.0.0");
	nvram_set("wan_get_dns", "");
	nvram_seti("wan_lease", 0);

	unlink("/tmp/get_lease_time");
	unlink("/tmp/lease_time");

}

void stop_ppp(webs_t wp)
{
	unlink("/tmp/ppp/log");
	unlink("/tmp/ppp/link");
}

static void validate_filter_tod(webs_t wp)
{
	char buf[256] = "";
	char tod_buf[20];
	struct variable filter_tod_variables[] = {
	      {argv:ARGV("20")},
	      {argv:ARGV("0", "1", "2")},

	}, *which;

	int day_all;
	int time_all, start_hour, start_min, end_hour, end_min;
	int _start_hour, _start_min, _end_hour, _end_min;
	char time[20];
	int week[7];
	int i, flag = -1, dash = 0;
	char filter_tod[] = "filter_todXXX";
	char filter_tod_buf[] = "filter_tod_bufXXX";

	which = &filter_tod_variables[0];

	day_all = websGetVari(wp, "day_all", 0);
	week[0] = websGetVari(wp, "week0", 0);
	week[1] = websGetVari(wp, "week1", 0);
	week[2] = websGetVari(wp, "week2", 0);
	week[3] = websGetVari(wp, "week3", 0);
	week[4] = websGetVari(wp, "week4", 0);
	week[5] = websGetVari(wp, "week5", 0);
	week[6] = websGetVari(wp, "week6", 0);
	time_all = websGetVari(wp, "time_all", 0);
	start_hour = websGetVari(wp, "start_hour", 0);
	start_min = websGetVari(wp, "start_min", 0);
	// start_time = websGetVari (wp, "start_time", 0);
	end_hour = websGetVari(wp, "end_hour", 0);
	end_min = websGetVari(wp, "end_min", 0);

	if (day_all == 1) {
		strcpy(time, "0-6");
		strcpy(tod_buf, "7");
	} else {
		strcpy(time, "");

		for (i = 0; i < 7; i++) {
			if (week[i] == 1) {
				if (i == 6) {
					if (dash == 0 && flag == 1)
						sprintf(time + strlen(time), "%c", '-');
					sprintf(time + strlen(time), "%d", i);
				} else if (flag == 1 && dash == 0) {
					sprintf(time + strlen(time), "%c", '-');
					dash = 1;
				} else if (dash == 0) {
					sprintf(time + strlen(time), "%d", i);
					flag = 1;
					dash = 0;
				}
			} else {
				if (!strcmp(time, ""))
					continue;
				if (dash == 1)
					sprintf(time + strlen(time), "%d", i - 1);
				if (flag != 0)
					sprintf(time + strlen(time), "%c", ',');
				flag = 0;
				dash = 0;
			}
		}
		if (time[strlen(time) - 1] == ',')
			time[strlen(time) - 1] = '\0';

		snprintf(tod_buf, sizeof(tod_buf), "%d %d %d %d %d %d %d", week[0], week[1], week[2], week[3], week[4], week[5], week[6]);
	}
	if (time_all == 1) {
		_start_hour = 0;
		_start_min = 0;
		_end_hour = 23;
		_end_min = 59;
	} else {
		_start_hour = start_hour;
		_start_min = start_min;
		_end_hour = end_hour;
		_end_min = end_min;
	}

	sprintf(buf, "%d:%d %d:%d %s", _start_hour, _start_min, _end_hour, _end_min, time);
	snprintf(filter_tod, sizeof(filter_tod), "filter_tod%d", wp->p->filter_id);
	snprintf(filter_tod_buf, sizeof(filter_tod_buf), "filter_tod_buf%d", wp->p->filter_id);

	nvram_set(filter_tod, buf);
	nvram_set(filter_tod_buf, tod_buf);
	D("everything okay");

}

void applytake(char *value)
{
	if (value && !strcmp(value, "ApplyTake")) {
		nvram_commit();
		service_restart();
	}
}

void save_policy(webs_t wp)
{
	char *f_name, *f_status, *f_status2;
	char *filter_if;
	char buf[256] = "";
	char *value = websGetVar(wp, "action", "");
	struct variable filter_variables[] = {
	      {argv:ARGV("1", "20")},
	      {argv:ARGV("0", "1", "2")},
	      {argv:ARGV("deny", "allow")},

	}, *which;
	char filter_buf[] = "filter_ruleXXX";

	D("save policy");
	which = &filter_variables[0];
	char *f_id = websGetVar(wp, "f_id", NULL);
	f_name = websGetVar(wp, "f_name", NULL);
	filter_if = websGetVar(wp, "filter_if", "Any");
	f_status = websGetVar(wp, "f_status", NULL);	// 0=>Disable /
	// 1,2=>Enable
	f_status2 = websGetVar(wp, "f_status2", NULL);	// deny=>Deny /
	// allow=>Allow
	if (!f_id || !f_name || !f_status || !f_status2) {
		D("invalid");
		return;
	}
	if (!valid_range(wp, f_id, &which[0])) {
		D("invalid");
		return;
	}
	if (!valid_choice(wp, f_status, &which[1])) {
		D("invalid");
		return;
	}
	if (!valid_choice(wp, f_status2, &which[2])) {
		D("invalid");
		return;
	}

	validate_filter_tod(wp);
	wp->p->filter_id = atoi(f_id);
	snprintf(filter_buf, sizeof(filter_buf), "filter_rule%d", wp->p->filter_id);

	// Add $DENY to decide that users select Allow or Deny, if status is
	// Disable // 2003/10/21
	snprintf(buf, sizeof(buf), "$STAT:%s$NAME:%s$DENY:%d$IF:%s$$", f_status, f_name, !strcmp(f_status2, "deny") ? 1 : 0, filter_if);

	nvram_set(filter_buf, buf);
	addAction("filters");
	applytake(value);

	D("okay");
}

void validate_filter_policy(webs_t wp, char *value, struct variable *v)
{
	wp->p->filter_id = websGetVari(wp, "f_id", 1);
	save_policy(wp);
}

char *num_to_protocol(int num)
{
	switch (num) {
	case 1:
		return "icmp";
	case 6:
		return "tcp";
	case 17:
		return "udp";
	case 23:
		return "both";
	case 99:
		return "l7";
	case 100:
		return "p2p";
#ifdef HAVE_OPENDPI
	case 101:
		return "dpi";
#endif
	default:
		return "unknown";
	}
}

/*
 * Format: 21:21:tcp:FTP(&nbsp;)500:1000:both:TEST1 
 */

void validate_services_port(webs_t wp)
{
	char *buf = (char *)calloc(8192, 1);
	char *services = (char *)calloc(8192, 1);
	char *cur = buf, *svcs = NULL;

	char *services_array = websGetVar(wp, "services_array0", NULL);

	// char *services_length = websGetVar (wp, "services_length0", NULL);
	char word[1026], *next;
	char delim[] = "(&nbsp;)";
	char var[32] = "";
	int index = 0;
	do {
		snprintf(var, sizeof(var), "services_array%d", index++);
		svcs = websGetVar(wp, var, NULL);
		if (svcs)
			strcat(services, svcs);

	}
	while (svcs);

	services_array = services;

	split(word, services_array, next, delim) {
		int from, to, proto;
		char name[80];

		if (sscanf(word, "%d:%d:%d:%s", &from, &to, &proto, name) != 4)
			continue;

		cur +=
		    snprintf(cur, buf + 8192 - cur,
			     "%s$NAME:%03d:%s$PROT:%03d:%s$PORT:%03d:%d:%d",
			     cur == buf ? "" : "<&nbsp;>", strlen(name), name, strlen(num_to_protocol(proto)), num_to_protocol(proto), (int)(get_int_len(from) + get_int_len(to) + strlen(":")), from, to);
	}

	// segment filter_services into <= 1024 byte lengths
	cur = buf;
	// fprintf (stderr, "cur=%s\n", cur);

	memcpy(word, cur, 1024);
	word[1025] = 0;
	nvram_set("filter_services", word);
	cur += 1024;

	if (strlen(cur) > 0) {
		nvram_set("filter_services_1", cur);
	}
	free(services);
	free(buf);
	// nvram_set ("filter_services", cur);
	D("okay");
}

void save_services_port(webs_t wp)
{
	validate_services_port(wp);
	char *value = websGetVar(wp, "action", "");
	applytake(value);
}

void delete_policy(webs_t wp, int which)
{
	D("delete policy");

	nvram_nset("", "filter_rule%d", which);
	nvram_nset("", "filter_tod%d", which);
	nvram_nset("", "filter_tod_buf%d", which);
	nvram_nset("", "filter_web_host%d", which);
	nvram_nset("", "filter_web_url%d", which);
	nvram_nset("", "filter_ip_grp%d", which);
	nvram_nset("", "filter_mac_grp%d", which);
	nvram_nset("", "filter_port_grp%d", which);
	nvram_nset("", "filter_dport_grp%d", which);

	D("okay");
}

void single_delete_policy(webs_t wp)
{
	D("single delete policy");
	delete_policy(wp, wp->p->filter_id);
	D("okay");
	return;
}

void summary_delete_policy(webs_t wp)
{
	int i;

	D("summary delete policy");
	for (i = 1; i <= 10; i++) {
		char filter_sum[] = "sumXXX";
		char *sum;

		snprintf(filter_sum, sizeof(filter_sum), "sum%d", i);
		sum = websGetVar(wp, filter_sum, NULL);
		if (sum)
			delete_policy(wp, i);
	}
	D("okay");
}

void addDeletion(char *word)
{
	if (!word || !strlen(word))
		return;

	char *oldarg = nvram_get("action_service_arg1");

	if (oldarg && strlen(oldarg) > 0) {
		char *newarg = safe_malloc(strlen(oldarg) + strlen(word) + 2);

		sprintf(newarg, "%s %s", oldarg, word);
		nvram_set("action_service_arg1", newarg);
		free(newarg);
	} else
		nvram_set("action_service_arg1", word);
}

void delete_old_routes(void)
{
	char word[256], *next;
	char ipaddr[20], netmask[20], gateway[20], met[20], ifn[20];

	sleep(1);
	foreach(word, nvram_safe_get("action_service_arg1"), next) {
		strcpy(ipaddr, strtok(word, ":"));
		strcpy(netmask, strtok(NULL, ":"));
		strcpy(gateway, strtok(NULL, ":"));
		strcpy(met, strtok(NULL, ":"));
		strcpy(ifn, strtok(NULL, ":"));

		route_del(ifn, atoi(met) + 1, ipaddr, gateway, netmask);
	}
}

void delete_static_route(webs_t wp)
{
	addAction("routing");
	nvram_seti("nowebaction", 1);
	char *buf = calloc(2500, 1);
	char *buf_name = calloc(2500, 1);

	char *cur = buf;
	char *cur_name = buf_name;
	char word[256], *next;
	char word_name[256], *next_name;
	int page = websGetVari(wp, "route_page", 0);
	char *value = websGetVar(wp, "action", "");
	int i = 0;
	char *performance = nvram_safe_get("static_route");
	char *performance2 = nvram_safe_get("static_route_name");

	foreach(word, performance, next) {
		if (i == page) {
			addDeletion(word);
			i++;
			continue;
		}

		cur += snprintf(cur, buf + 2500 - cur, "%s%s", cur == buf ? "" : " ", word);

		i++;
	}

	i = 0;
	foreach(word_name, performance2, next_name) {
		if (i == page) {
			i++;
			continue;
		}
		cur_name += snprintf(cur_name, buf_name + 2500 - cur_name, "%s%s", cur_name == buf_name ? "" : " ", word_name);

		i++;
	}

	nvram_set("static_route", buf);
	nvram_set("static_route_name", buf_name);
	free(buf_name);
	free(buf);
	applytake(value);
	return;
}

extern void gen_key(webs_t wp, char *genstr, int weptype, unsigned char key64[4][5], unsigned char key128[4][14]);

void generate_wep_key_single(webs_t wp, char *prefix, char *passphrase, char *bit, char *tx)
{

	int i;
	char buf[256];
	char var[80];
	unsigned char key128[4][14];
	unsigned char key64[4][5];

	if (!prefix || !bit || !passphrase || !tx)
		return;

	gen_key(wp, passphrase, atoi(bit), key64, key128);

	wp->p->generate_key = 1;

	if (atoi(bit) == 64) {
		char key1[27] = "";
		char key2[27] = "";
		char key3[27] = "";
		char key4[27] = "";

		for (i = 0; i < 5; i++)
			sprintf(key1 + (i << 1), "%02X", key64[0][i]);
		for (i = 0; i < 5; i++)
			sprintf(key2 + (i << 1), "%02X", key64[1][i]);
		for (i = 0; i < 5; i++)
			sprintf(key3 + (i << 1), "%02X", key64[2][i]);
		for (i = 0; i < 5; i++)
			sprintf(key4 + (i << 1), "%02X", key64[3][i]);

		snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", passphrase, key1, key2, key3, key4, tx);
		// nvram_set("wl_wep_gen_64",buf);
		cprintf("buf = %s\n", buf);
		sprintf(var, "%s_wep_gen", prefix);

		nvram_set(var, buf);
		nvram_nset(key1, "%s_key1", prefix);
		nvram_nset(key2, "%s_key2", prefix);
		nvram_nset(key3, "%s_key3", prefix);
		nvram_nset(key4, "%s_key4", prefix);
	} else if (atoi(bit) == 128) {
		char key1[27] = "";
		char key2[27] = "";
		char key3[27] = "";
		char key4[27] = "";

		for (i = 0; i < 13; i++)
			sprintf(key1 + (i << 1), "%02X", key128[0][i]);
		key1[26] = 0;

		for (i = 0; i < 13; i++)
			sprintf(key2 + (i << 1), "%02X", key128[1][i]);
		key2[26] = 0;

		for (i = 0; i < 13; i++)
			sprintf(key3 + (i << 1), "%02X", key128[2][i]);
		key3[26] = 0;

		for (i = 0; i < 13; i++)
			sprintf(key4 + (i << 1), "%02X", key128[3][i]);
		key4[26] = 0;
		// cprintf("passphrase[%s]\n", passphrase);
		// filter_name(passphrase, new_passphrase, sizeof(new_passphrase),
		// SET);
		// cprintf("new_passphrase[%s]\n", new_passphrase);
		cprintf("key1 = %s\n", key1);
		cprintf("key2 = %s\n", key2);
		cprintf("key3 = %s\n", key3);
		cprintf("key4 = %s\n", key4);

		snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", passphrase, key1, key2, key3, key4, tx);
		cprintf("buf = %s\n", buf);
		// nvram_set("wl_wep_gen_128",buf);
		sprintf(var, "%s_wep_gen", prefix);
		nvram_set(var, buf);
		nvram_nset(key1, "%s_key1", prefix);
		nvram_nset(key2, "%s_key2", prefix);
		nvram_nset(key3, "%s_key3", prefix);
		nvram_nset(key4, "%s_key4", prefix);
	}
	return;
}

void generate_wep_key(webs_t wp)
{
	char *prefix, *passphrase, *bit, *tx;

#ifdef HAVE_MADWIFI
	prefix = websGetVar(wp, "security_varname", "ath0");
#else
	prefix = websGetVar(wp, "security_varname", "wl");
#endif
	char var[80];

	sprintf(var, "%s_wep_bit", prefix);
	bit = websGetVar(wp, var, NULL);
	if (bit != NULL)
		nvram_set("wl_wep_bit", bit);
	sprintf(var, "%s_passphrase", prefix);
	passphrase = websGetVar(wp, var, NULL);
	sprintf(var, "%s_key", prefix);
	tx = websGetVar(wp, var, NULL);
	cprintf("gen wep key: bits = %s\n", bit);

	generate_wep_key_single(wp, prefix, passphrase, bit, tx);
}

static char *_copytonv(webs_t wp, const char *fmt, ...)
{
	char varbuf[64];
	va_list args;

	va_start(args, (char *)fmt);
	vsnprintf(varbuf, sizeof(varbuf), fmt, args);
	va_end(args);

	char *wl = websGetVar(wp, varbuf, NULL);
	dd_debug(DEBUG_HTTPD, "save %s with value %s\n", varbuf, wl);
	nvram_set(varbuf, wl);
	return wl;
}

char *copytonv(webs_t wp, const char *fmt, ...)
{
	char varbuf[64];
	va_list args;

	va_start(args, (char *)fmt);
	vsnprintf(varbuf, sizeof(varbuf), fmt, args);
	va_end(args);

	char *wl = websGetVar(wp, varbuf, NULL);
	dd_debug(DEBUG_HTTPD, "save %s with value %s\n", varbuf, wl);
	if (wl)
		nvram_set(varbuf, wl);
	return wl;
}

void copymergetonv(webs_t wp, const char *fmt, ...)
{
	char varbuf[64];
	va_list args;

	va_start(args, (char *)fmt);
	vsnprintf(varbuf, sizeof(varbuf), fmt, args);
	va_end(args);
	char ipaddr[32];
	if (get_merge_ipaddr(wp, varbuf, ipaddr)) {
		nvram_set(varbuf, ipaddr);
	}

}

void copytonv2(webs_t wp, char *prefix_get, char *prefix_set, char *name)
{
	char tmpname[64];

	sprintf(tmpname, "%s_%s", prefix_get, name);

	char *wl = websGetVar(wp, tmpname, NULL);

	sprintf(tmpname, "%s_%s", prefix_set, name);

	if (wl)
		nvram_set(tmpname, wl);
}

void copytonv2_wme(webs_t wp, char *prefix_get, char *prefix_set, char *name, int maxindex)
{
	char tmpvalue[128] = "";
	char tmpname[64];
	char *next;
	char *wl;
	int i;

	for (i = 0; i <= maxindex; i++) {
		sprintf(tmpname, "%s_%s%d", prefix_get, name, i);
		wl = websGetVar(wp, tmpname, NULL);
		if (wl) {
			strcat(tmpvalue, wl);
			strcat(tmpvalue, " ");
		}
	}

	sprintf(tmpname, "%s_%s", prefix_set, name);
	strtrim_right(tmpvalue, ' ');
	nvram_set(tmpname, tmpvalue);
}

static void save_secprefix(webs_t wp, char *prefix)
{
	char n[80];
	char radius[80];
	char p2[80];

	strcpy(p2, prefix);
	if (strchr(prefix, '.'))
		rep(p2, '.', 'X');	// replace invalid characters for sub ifs

#ifdef HAVE_WPA_SUPPLICANT

/*_8021xtype
_8021xuser
_8021xpasswd
_8021xca
_8021xpem
_8021xprv
*/
	copytonv(wp, "%s_8021xtype", prefix);
	copytonv(wp, "%s_tls8021xuser", prefix);
	copytonv(wp, "%s_tls8021xanon", prefix);
	copytonv(wp, "%s_tls8021xpasswd", prefix);
	copytonv(wp, "%s_tls8021xphase2", prefix);
	copytonv(wp, "%s_tls8021xca", prefix);
	copytonv(wp, "%s_tls8021xpem", prefix);
	copytonv(wp, "%s_tls8021xprv", prefix);
	copytonv(wp, "%s_tls8021xaddopt", prefix);
	copytonv(wp, "%s_peap8021xuser", prefix);
	copytonv(wp, "%s_peap8021xanon", prefix);
	copytonv(wp, "%s_peap8021xpasswd", prefix);
	copytonv(wp, "%s_tls8021xkeyxchng", prefix);
	copytonv(wp, "%s_peap8021xphase2", prefix);
	copytonv(wp, "%s_peap8021xca", prefix);
	copytonv(wp, "%s_peap8021xaddopt", prefix);
	copytonv(wp, "%s_ttls8021xuser", prefix);
	copytonv(wp, "%s_ttls8021xanon", prefix);
	copytonv(wp, "%s_ttls8021xpasswd", prefix);
	copytonv(wp, "%s_ttls8021xphase2", prefix);
	copytonv(wp, "%s_ttls8021xca", prefix);
	copytonv(wp, "%s_ttls8021xaddopt", prefix);
	copytonv(wp, "%s_leap8021xuser", prefix);
	copytonv(wp, "%s_leap8021xanon", prefix);
	copytonv(wp, "%s_leap8021xpasswd", prefix);
	copytonv(wp, "%s_leap8021xphase2", prefix);
	copytonv(wp, "%s_leap8021xaddopt", prefix);

#endif
	copytonv(wp, "%s_wpa_psk", prefix);
#ifdef HAVE_MADWIFI
	copytonv(wp, "%s_sae_key", prefix);
#endif
	copytonv(wp, "%s_disable_eapol_key_retries", prefix);
#ifdef HAVE_80211R
	copytonv(wp, "%s_ft", prefix);
	copytonv(wp, "%s_domain", prefix);
	copytonv(wp, "%s_nas", prefix);
#endif
#ifdef HAVE_80211W
	copytonv(wp, "%s_mfp", prefix);
#endif
	copytonv(wp, "%s_wpa_gtk_rekey", prefix);
	copymergetonv(wp, "%s_radius_ipaddr", prefix);
	copytonv(wp, "%s_radius_port", prefix);
	copytonv(wp, "%s_radius_key", prefix);

	copymergetonv(wp, "%s_local_ip", prefix);

	copymergetonv(wp, "%s_radius2_ipaddr", prefix);
	copytonv(wp, "%s_radius2_port", prefix);
	copytonv(wp, "%s_radius2_key", prefix);
#ifdef HAVE_MADWIFI
	copytonv(wp, "%s_radius_retry", prefix);
	copytonv(wp, "%s_acct", prefix);
	copymergetonv(wp, "%s_acct_ipaddr", prefix);
	copytonv(wp, "%s_acct_port", prefix);
	copytonv(wp, "%s_acct_key", prefix);
#endif
	copytonv(wp, "%s_radmactype", prefix);

	sprintf(n, "%s_authmode", prefix);
	char *authmode = websGetVar(wp, n, "");
	if (strlen(authmode) == 0) {
		nvram_set(n, "open");
	} else {
		copytonv(wp, n);
	}
	sprintf(n, "%s_key1", prefix);
	char *key1 = websGetVar(wp, n, "");

	copytonv(wp, n);
	sprintf(n, "%s_key2", prefix);
	char *key2 = websGetVar(wp, n, "");

	copytonv(wp, n);
	sprintf(n, "%s_key3", prefix);
	char *key3 = websGetVar(wp, n, "");

	copytonv(wp, n);
	sprintf(n, "%s_key4", prefix);
	char *key4 = websGetVar(wp, n, "");

	copytonv(wp, n);
	sprintf(n, "%s_passphrase", prefix);
	char *pass = websGetVar(wp, n, "");

	copytonv(wp, n);
	sprintf(n, "%s_key", prefix);
	char *tx = websGetVar(wp, n, "");
	if (strlen(tx) == 0) {
		nvram_seti(n, 1);
	} else {
		copytonv(wp, n);
	}
	copytonv(wp, "%s_wep_bit", prefix);
	char buf[128];

	snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", pass, key1, key2, key3, key4, tx);
	sprintf(n, "%s_wep_buf", prefix);
	nvram_set(n, buf);

	sprintf(n, "%s_security_mode", p2);
	char n2[80];

	char *v = websGetVar(wp, n, NULL);
	sprintf(n2, "%s_akm", prefix);

#ifdef HAVE_MADWIFI
	if (v && (!strcmp(v, "wpa") || !strcmp(v, "8021X"))) {
		_copytonv(wp, "%s_ccmp", prefix);
		_copytonv(wp, "%s_tkip", prefix);
		_copytonv(wp, "%s_ccmp-256", prefix);
		_copytonv(wp, "%s_gcmp-256", prefix);
		_copytonv(wp, "%s_gcmp", prefix);
	}
#else
	copytonv(wp, "%s_crypto", prefix);
#endif

	if (v) {
		char auth[32];
		char wep[32];

		sprintf(auth, "%s_auth_mode", prefix);
		sprintf(wep, "%s_wep", prefix);
		if (!strcmp(v, "wep")) {
			nvram_set(auth, "none");
			nvram_set(wep, "enabled");
		} else if (!strcmp(v, "radius")) {
			nvram_set(auth, "radius");
			nvram_set(wep, "enabled");
		} else {
			nvram_set(auth, "none");
			nvram_set(wep, "disabled");
		}
		nvram_set(n2, v);
	}
#ifdef HAVE_MADWIFI

	if (v && !strcmp(v, "wpa")) {
		_copytonv(wp, "%s_psk", prefix);
		_copytonv(wp, "%s_psk2", prefix);
		_copytonv(wp, "%s_psk2-sha256", prefix);
		_copytonv(wp, "%s_psk3", prefix);
		_copytonv(wp, "%s_wpa", prefix);
		_copytonv(wp, "%s_wpa2", prefix);
		_copytonv(wp, "%s_wpa2-sha256", prefix);
		_copytonv(wp, "%s_wpa3", prefix);
		_copytonv(wp, "%s_wpa3-192", prefix);
		_copytonv(wp, "%s_wpa3-128", prefix);
		char akm[128] = { 0, 0 };
		if (nvram_nmatch("1", "%s_psk", prefix))
			sprintf(akm, "%s %s", akm, "psk");
		if (nvram_nmatch("1", "%s_psk2", prefix))
			sprintf(akm, "%s %s", akm, "psk2");
		if (nvram_nmatch("1", "%s_psk2-sha256", prefix))
			sprintf(akm, "%s %s", akm, "psk2-sha256");
		if (nvram_nmatch("1", "%s_psk3", prefix))
			sprintf(akm, "%s %s", akm, "psk3");
		if (nvram_nmatch("1", "%s_wpa", prefix))
			sprintf(akm, "%s %s", akm, "wpa");
		if (nvram_nmatch("1", "%s_wpa2", prefix))
			sprintf(akm, "%s %s", akm, "wpa2");
		if (nvram_nmatch("1", "%s_wpa2-sha256", prefix))
			sprintf(akm, "%s %s", akm, "wpa2-sha256");
		if (nvram_nmatch("1", "%s_wpa3", prefix))
			sprintf(akm, "%s %s", akm, "wpa3");
		if (nvram_nmatch("1", "%s_wpa3-192", prefix))
			sprintf(akm, "%s %s", akm, "wpa3-192");
		if (nvram_nmatch("1", "%s_wpa3-128", prefix))
			sprintf(akm, "%s %s", akm, "wpa3-128");

		nvram_set(n2, &akm[1]);
	}

	if (v && !strcmp(v, "8021X")) {
		_copytonv(wp, "%s_802.1x", prefix);
		_copytonv(wp, "%s_leap", prefix);
		_copytonv(wp, "%s_peap", prefix);
		_copytonv(wp, "%s_tls", prefix);
		_copytonv(wp, "%s_ttls", prefix);
		_copytonv(wp, "%s_wpa", prefix);
		_copytonv(wp, "%s_wpa2", prefix);
		_copytonv(wp, "%s_wpa2-sha256", prefix);
		_copytonv(wp, "%s_wpa3", prefix);
		_copytonv(wp, "%s_wpa3-192", prefix);
		_copytonv(wp, "%s_wpa3-128", prefix);
		char akm[128] = { 0, 0 };
		if (nvram_nmatch("1", "%s_leap", prefix))
			sprintf(akm, "%s %s", akm, "leap");
		if (nvram_nmatch("1", "%s_peap", prefix))
			sprintf(akm, "%s %s", akm, "peap");
		if (nvram_nmatch("1", "%s_tls", prefix))
			sprintf(akm, "%s %s", akm, "tls");
		if (nvram_nmatch("1", "%s_ttls", prefix))
			sprintf(akm, "%s %s", akm, "ttls");
		if (nvram_nmatch("1", "%s_wpa", prefix))
			sprintf(akm, "%s %s", akm, "wpa");
		if (nvram_nmatch("1", "%s_wpa2", prefix))
			sprintf(akm, "%s %s", akm, "wpa2");
		if (nvram_nmatch("1", "%s_wpa2-sha256", prefix))
			sprintf(akm, "%s %s", akm, "wpa2-sha256");
		if (nvram_nmatch("1", "%s_wpa3", prefix))
			sprintf(akm, "%s %s", akm, "wpa3");
		if (nvram_nmatch("1", "%s_wpa3-192", prefix))
			sprintf(akm, "%s %s", akm, "wpa3-192");
		if (nvram_nmatch("1", "%s_wpa3-128", prefix))
			sprintf(akm, "%s %s", akm, "wpa3-128");
		nvram_set(n2, &akm[1]);
	}

#endif
	copytonv(wp, n);
#ifdef HAVE_MADWIFI
	sprintf(n, "%s_config", p2);
	sprintf(n2, "%s_config", prefix);
	v = websGetVar(wp, n, NULL);
	if (v && strlen(v) > 0) {
		nvram_set(n2, v);
	} else {
		nvram_unset(n2);
	}

#endif

}

static int security_save_prefix(webs_t wp, char *prefix)
{

	save_secprefix(wp, prefix);
	char *next;
	char var[80];
	char *vifs = nvram_nget("%s_vifs", prefix);

	if (vifs == NULL)
		return 0;
	foreach(var, vifs, next) {
		save_secprefix(wp, var);
	}
	// nvram_commit ();
	return 0;
}

void security_save(webs_t wp)
{
	char *value = websGetVar(wp, "action", "");

#ifdef HAVE_MADWIFI
	int dc = getdevicecount();
	int i;

	for (i = 0; i < dc; i++) {
		char b[16];

		sprintf(b, "ath%d", i);
		security_save_prefix(wp, b);
	}
#else
	int dc = get_wl_instances();
	int i;

	for (i = 0; i < dc; i++) {
		char b[16];

		sprintf(b, "wl%d", i);
		security_save_prefix(wp, b);
	}
#endif
	applytake(value);
}

void add_active_mac(webs_t wp)
{
	int i, count = 0;
	int msize = 4608;	// 18 chars * 256 entries
	char *buf = calloc(msize, 1);
	char *cur = buf;
	char *ifname = websGetVar(wp, "ifname", NULL);

	nvram_seti("wl_active_add_mac", 1);

	for (i = 0; i < MAX_LEASES + 2; i++) {
		char active_mac[] = "onXXX";
		int index;

		snprintf(active_mac, sizeof(active_mac), "%s%d", "on", i);
		index = websGetVari(wp, active_mac, 0);

		count++;

		cur += snprintf(cur, buf + msize - cur, "%s%s", cur == buf ? "" : " ", wp->p->wl_client_macs[index].hwaddr);
	}
	for (i = 0; i < MAX_LEASES + 2; i++) {
		char active_mac[] = "offXXX";
		int index;

		snprintf(active_mac, sizeof(active_mac), "%s%d", "off", i);
		index = websGetVari(wp, active_mac, 0);

		count++;
		cur += snprintf(cur, buf + msize - cur, "%s%s", cur == buf ? "" : " ", wp->p->wl_client_macs[index].hwaddr);
	}
	char acmac[32];
	sprintf(acmac, "%s_active_mac", ifname);
	nvram_set(acmac, buf);
	if (!strcmp(ifname, "wl0"))
		nvram_set("wl_active_mac", buf);
	free(buf);
}

void removeLineBreak(char *startup)
{
	int i = 0;
	int c = 0;

	for (i = 0; i < strlen(startup); i++) {
		if (startup[i] == '\r')
			continue;
		startup[c++] = startup[i];
	}
	startup[c++] = 0;

}

void ping_startup(webs_t wp)
{
	char *startup = websGetVar(wp, "ping_ip", NULL);
	if (startup) {
		// filter Windows <cr>ud
		removeLineBreak(startup);

		nvram_set("rc_startup", startup);
		nvram_commit();
		nvram2file("rc_startup", "/tmp/.rc_startup");
		chmod("/tmp/.rc_startup", 0700);
	}
	return;

}

void ping_shutdown(webs_t wp)
{
	char *shutdown = websGetVar(wp, "ping_ip", NULL);
	if (shutdown) {
		// filter Windows <cr>ud
		removeLineBreak(shutdown);

		nvram_set("rc_shutdown", shutdown);
		nvram_commit();
		nvram2file("rc_shutdown", "/tmp/.rc_shutdown");
		chmod("/tmp/.rc_shutdown", 0700);
	}
	return;

}

void ping_firewall(webs_t wp)
{
	char *firewall = websGetVar(wp, "ping_ip", NULL);
	if (firewall) {
		// filter Windows <cr>ud
		removeLineBreak(firewall);
		nvram_set("rc_firewall", firewall);
		nvram_commit();
		nvram2file("rc_firewall", "/tmp/.rc_firewall");
		chmod("/tmp/.rc_firewall", 0700);
	}
	return;
}

void ping_custom(webs_t wp)
{
	char *custom = websGetVar(wp, "ping_ip", NULL);
	if (custom) {
		// filter Windows <cr>ud
		unlink("/tmp/custom.sh");
		removeLineBreak(custom);
		nvram_set("rc_custom", custom);
		nvram_commit();
		if (nvram_invmatch("rc_custom", "")) {
			nvram2file("rc_custom", "/tmp/custom.sh");
			chmod("/tmp/custom.sh", 0700);
		}
	}

	return;
}

void ping_wol(webs_t wp)
{
	char *wol_type = websGetVar(wp, "wol_type", NULL);

	unlink(PING_TMP);

	if (!wol_type || !strcmp(wol_type, ""))
		return;

	if (!strcmp(wol_type, "update")) {
		char *wol_hosts = websGetVar(wp, "wol_hosts", NULL);

		if (!wol_hosts || !strcmp(wol_hosts, ""))
			return;

		nvram_set("wol_hosts", wol_hosts);
		nvram_set("wol_cmd", "");
		return;
	}

	char *manual_wol_mac = websGetVar(wp, "manual_wol_mac", NULL);
	char *manual_wol_network = websGetVar(wp, "manual_wol_network", NULL);
	char *manual_wol_port = websGetVar(wp, "manual_wol_port", NULL);

	if (!strcmp(wol_type, "manual")) {
		nvram_set("manual_wol_mac", manual_wol_mac);
		nvram_set("manual_wol_network", manual_wol_network);
		nvram_set("manual_wol_port", manual_wol_port);
	}

	char wol_cmd[256] = { 0 };
	snprintf(wol_cmd, sizeof(wol_cmd), "/usr/sbin/wol -v -i %s -p %s %s", manual_wol_network, manual_wol_port, manual_wol_mac);
	nvram_set("wol_cmd", wol_cmd);

	// use Wol.asp as a debugging console
#ifdef HAVE_REGISTER
	if (!wp->isregistered_real)
		return;
#endif
	FILE *fp = popen(wol_cmd, "rb");
	FILE *out = fopen(PING_TMP, "wb");
	if (!fp)
		return;
	if (out) {
		while (!feof(fp))
			putc(getc(fp), out);
		fclose(out);
	}
	pclose(fp);
}

void diag_ping_start(webs_t wp)
{
	char *ip = websGetVar(wp, "ping_ip", NULL);

	if (!ip || !strcmp(ip, ""))
		return;

	unlink(PING_TMP);
	nvram_set("ping_ip", ip);

	setenv("PATH", "/sbin:/bin:/usr/sbin:/usr/bin", 1);
#ifdef HAVE_REGISTER
	if (!wp->isregistered_real)
		return;
#endif
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "alias ping=\'ping -c 3\'; eval \"%s\" > %s 2>&1 &", ip, PING_TMP);
	//FORK(system(cmd));

	FILE *fp = popen(cmd, "rb");
	if (!fp)
		return;
	while (!feof(fp))
		getc(fp);
	pclose(fp);

	return;
}

void diag_ping_stop(webs_t wp)
{
	killall("ping", SIGKILL);
}

void diag_ping_clear(webs_t wp)
{
	unlink(PING_TMP);
}

void save_wireless_advanced(webs_t wp)
{
	char set_prefix[8];
	char prefix[8];
	char *wlface = websGetVar(wp, "interface", NULL);

	if (!strcmp(wlface, "wl0"))
		sprintf(set_prefix, "%s", "wl");
	else
		sprintf(set_prefix, "%s", wlface);

	sprintf(prefix, wlface);

	copytonv2(wp, prefix, set_prefix, "auth");
	copytonv2(wp, prefix, set_prefix, "rateset");
	copytonv2(wp, prefix, set_prefix, "nmcsidx");
	copytonv2(wp, prefix, set_prefix, "rate");
	copytonv2(wp, prefix, set_prefix, "gmode_protection");
	copytonv2(wp, prefix, set_prefix, "frameburst");
	copytonv2(wp, prefix, set_prefix, "bcn");
	copytonv2(wp, prefix, set_prefix, "dtim");
	copytonv2(wp, prefix, set_prefix, "frag");
	copytonv2(wp, prefix, set_prefix, "rts");
	copytonv2(wp, prefix, set_prefix, "maxassoc");
	copytonv2(wp, prefix, set_prefix, "ap_isolate");
	copytonv2(wp, prefix, set_prefix, "plcphdr");
	copytonv2(wp, prefix, set_prefix, "shortslot");
	copytonv2(wp, prefix, set_prefix, "afterburner");
	copytonv2(wp, prefix, set_prefix, "btc_mode");
	copytonv2(wp, prefix, set_prefix, "wme");
	copytonv2(wp, prefix, set_prefix, "wme_no_ack");
	copytonv2_wme(wp, prefix, set_prefix, "wme_ap_bk", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_ap_be", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_ap_vi", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_ap_vo", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_sta_bk", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_sta_be", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_sta_vi", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_sta_vo", 5);
	copytonv2_wme(wp, prefix, set_prefix, "wme_txp_bk", 4);
	copytonv2_wme(wp, prefix, set_prefix, "wme_txp_be", 4);
	copytonv2_wme(wp, prefix, set_prefix, "wme_txp_vi", 4);
	copytonv2_wme(wp, prefix, set_prefix, "wme_txp_vo", 4);

	return;

}

void save_wds(webs_t wp)
{
	char *wds_enable_val, wds_enable_var[32] = { 0 };
	int h = 0;
	char *interface = websGetVar(wp, "interface", NULL);

	for (h = 1; h <= MAX_WDS_DEVS; h++) {
		sprintf(wds_enable_var, "%s_wds%d_enable", interface, h);
		wds_enable_val = websGetVar(wp, wds_enable_var, NULL);
		nvram_set(wds_enable_var, wds_enable_val);
	}
	sprintf(wds_enable_var, "%s_br1_enable", interface);
	wds_enable_val = websGetVar(wp, wds_enable_var, NULL);
	nvram_set(wds_enable_var, wds_enable_val);

	sprintf(wds_enable_var, "%s_br1_nat", interface);
	wds_enable_val = websGetVar(wp, wds_enable_var, NULL);
	nvram_set(wds_enable_var, wds_enable_val);

	return;

}

int get_svc(char *svc, char *protocol, char *ports)
{
	char word[1024], *next;
	char delim[] = "<&nbsp;>";
	char *services;
	// services = nvram_safe_get("filter_services");
	services = get_filter_services();

	split(word, services, next, delim) {
		int len = 0;
		char *name, *prot, *port;
		int from = 0, to = 0;

		if ((name = strstr(word, "$NAME:")) == NULL || (prot = strstr(word, "$PROT:")) == NULL || (port = strstr(word, "$PORT:")) == NULL)
			continue;

		/*
		 * $NAME 
		 */
		if (sscanf(name, "$NAME:%3d:", &len) != 1)
			return -1;

		strncpy(name, name + sizeof("$NAME:nnn:") - 1, len);
		name[len] = '\0';

		if (strcasecmp(svc, name))
			continue;

		/*
		 * $PROT 
		 */
		if (sscanf(prot, "$PROT:%3d:", &len) != 1)
			return -1;

		strncpy(protocol, prot + sizeof("$PROT:nnn:") - 1, len);
		protocol[len] = '\0';

		/*
		 * $PORT 
		 */
		if (sscanf(port, "$PORT:%3d:", &len) != 1)
			return -1;

		strncpy(ports, port + sizeof("$PORT:nnn:") - 1, len);
		ports[len] = '\0';

		if (sscanf(ports, "%d:%d", &from, &to) != 2) {
			free(services);
			return -1;
		}

		if (strcasecmp(svc, name) == 0) {
			free(services);
			return 0;
		}
	}
	free(services);

	return -1;
}

void qos_add_svc(webs_t wp)
{
	char *var = websGetVar(wp, "wshaper_enable", NULL);

	if (var != NULL)
		nvram_set("wshaper_enable", var);

	char protocol[100] = { 0 }, ports[100] = {
	0};
	char *add_svc = websGetVar(wp, "add_svc", NULL);
	char *svqos_svcs = nvram_safe_get("svqos_svcs");
	char *new_svcs;
	int i = 0;
	if (!add_svc)
		return;

	if (get_svc(add_svc, protocol, ports))
		return;

	if (strcmp(protocol, "l7") == 0) {
		int slen = strlen(add_svc);

		for (i = 0; i < slen; i++)
			add_svc[i] = tolower(add_svc[i]);
	}
#ifdef HAVE_OPENDPI
	if (strcmp(protocol, "dpi") == 0) {
		int slen = strlen(add_svc);

		for (i = 0; i < slen; i++)
			add_svc[i] = tolower(add_svc[i]);
	}
#endif

	/*
	 * if this service exists, return an error 
	 */
	if (strstr(svqos_svcs, add_svc))
		return;

	if (strlen(svqos_svcs) > 0)
		asprintf(&new_svcs, "%s %s %s %s 30 |", svqos_svcs, add_svc, protocol, ports);
	else
		asprintf(&new_svcs, "%s %s %s 30 |", add_svc, protocol, ports);

//      if (strlen(new_svcs) >= sizeof(new_svcs)) //this check is just stupid. it means overflow
//              return;

	nvram_set("svqos_svcs", new_svcs);
	nvram_commit();
	free(new_svcs);
}

void qos_add_dev(webs_t wp)
{
	char *var = websGetVar(wp, "wshaper_enable", NULL);

	if (var != NULL)
		nvram_set("wshaper_enable", var);

	char *add_dev = websGetVar(wp, "svqos_dev", NULL);
	char *svqos_ips = nvram_safe_get("svqos_devs");
	char *new_ip;
	if (!add_dev)
		return;
	/*
	 * if this ip exists, return an error 
	 */
#ifdef HAVE_AQOS
	asprintf(&new_ip, "%s %s 100 100 0 0 none |", svqos_ips, add_dev);
#else
	asprintf(&new_ip, "%s %s 30 |", svqos_ips, add_dev);
#endif

	nvram_set("svqos_devs", new_ip);
	free(new_ip);
}

void qos_add_ip(webs_t wp)
{
	char *var = websGetVar(wp, "wshaper_enable", NULL);

	if (var != NULL)
		nvram_set("wshaper_enable", var);

	char *add_ip0 = websGetVar(wp, "svqos_ipaddr0", NULL);
	char *add_ip1 = websGetVar(wp, "svqos_ipaddr1", NULL);
	char *add_ip2 = websGetVar(wp, "svqos_ipaddr2", NULL);
	char *add_ip3 = websGetVar(wp, "svqos_ipaddr3", NULL);
	char *add_nm = websGetVar(wp, "svqos_netmask", NULL);
	char add_ip[19] = { 0 };
	char *svqos_ips = nvram_safe_get("svqos_ips");
	char *new_ip;
	if (!svqos_ips || !add_ip0 || !add_ip1 || !add_ip2 || !add_ip3 || !add_nm)
		return;

	snprintf(add_ip, sizeof(add_ip), "%s.%s.%s.%s/%s", add_ip0, add_ip1, add_ip2, add_ip3, add_nm);

	/*
	 * if this ip exists, return an error 
	 */
	if (strstr(svqos_ips, add_ip))
		return;
#ifdef HAVE_AQOS
	asprintf(&new_ip, "%s %s 100 100 0 0 |", svqos_ips, add_ip);
#else
	asprintf(&new_ip, "%s %s 30 |", svqos_ips, add_ip);
#endif

	nvram_set("svqos_ips", new_ip);
	free(new_ip);

}

void qos_add_mac(webs_t wp)
{
	char *var = websGetVar(wp, "wshaper_enable", NULL);

	if (var != NULL)
		nvram_set("wshaper_enable", var);

	char *add_mac0 = websGetVar(wp, "svqos_hwaddr0", NULL);
	char *add_mac1 = websGetVar(wp, "svqos_hwaddr1", NULL);
	char *add_mac2 = websGetVar(wp, "svqos_hwaddr2", NULL);
	char *add_mac3 = websGetVar(wp, "svqos_hwaddr3", NULL);
	char *add_mac4 = websGetVar(wp, "svqos_hwaddr4", NULL);
	char *add_mac5 = websGetVar(wp, "svqos_hwaddr5", NULL);
	char *svqos_macs = nvram_safe_get("svqos_macs");
	char *new_mac;
	if (!svqos_macs || !add_mac0 || !add_mac1 || !add_mac2 || !add_mac3 || !add_mac4 || !add_mac5)
		return;

	char add_mac[19];
	snprintf(add_mac, sizeof(add_mac), "%s:%s:%s:%s:%s:%s", add_mac0, add_mac1, add_mac2, add_mac3, add_mac4, add_mac5);
	/*
	 * if this mac exists, return an error 
	 */
	if (strstr(svqos_macs, add_mac))
		return;
#ifdef HAVE_AQOS
	asprintf(&new_mac, "%s %s 100 100 user 0 0 |", svqos_macs, add_mac);
#else
	asprintf(&new_mac, "%s %s 30 |", svqos_macs, add_mac);
#endif

	nvram_set("svqos_macs", new_mac);
	free(new_mac);

}

#ifdef HAVE_EOP_TUNNEL
void tunnel_save(webs_t wp)
{
	int i;
	int tunnels = nvram_geti("oet_tunnels");
	for (i = 1; i < tunnels + 1; i++) {
		copytonv(wp, "oet%d_en", i);
		copytonv(wp, "oet%d_proto", i);
		copytonv(wp, "oet%d_peers", i);
		copytonv(wp, "oet%d_id", i);
		copytonv(wp, "oet%d_bridged", i);
		copytonv(wp, "oet%d_port", i);
		copymergetonv(wp, "oet%d_rem", i);
		copymergetonv(wp, "oet%d_local", i);
		copymergetonv(wp, "oet%d_ipaddr", i);
		copymergetonv(wp, "oet%d_netmask", i);
		char temp[32];
		sprintf(temp, "oet%d_peers", i);
		int peers = nvram_geti(temp);
		int peer;
		for (peer = 0; peer < peers; peer++) {
			copytonv(wp, "oet%d_endpoint%d", i, peer);
			copytonv(wp, "oet%d_peerkey%d", i, peer);
			copytonv(wp, "oet%d_ka%d", i, peer);
			copytonv(wp, "oet%d_aip%d", i, peer);
			copytonv(wp, "oet%d_peerport%d", i, peer);
			copytonv(wp, "oet%d_rem%d", i, peer);
			copytonv(wp, "oet%d_usepsk%d", i, peer);
			copytonv(wp, "oet%d_psk%d", i, peer);
		}
	}
	char *value = websGetVar(wp, "action", "");
	applytake(value);
}

#ifdef HAVE_WIREGUARD
void gen_wg_key(webs_t wp)
{
	tunnel_save(wp);
	int key = websGetVari(wp, "keyindex", -1);
	if (key < 0)
		return;
	char idx[32];
	sprintf(idx, "%d", key);
	eval("makewgkey", idx);
}

void add_peer(webs_t wp)
{
	tunnel_save(wp);
	int key = websGetVari(wp, "keyindex", -1);
	char idx[32];
	sprintf(idx, "oet%d_peers", key);
	nvram_default_get(idx, "0");
	int peer = nvram_geti(idx);

#define default_set(name,val) if (strlen(nvram_nget("oet%d_%s%d",key,name,peer))==0)nvram_nset(val, "oet%d_%s%d",key,name,peer)
	default_set("ka", "0");
	default_set("endpoint", "0");
	default_set("usepsk", "0");
	default_set("rem", "0.0.0.0");
	default_set("peerport", "51280");
#undef default_set
	peer++;
	nvram_seti(idx, peer);
}

static void copypeervalue(char *valuename, int tun, int from, int to)
{
	char name[32];
	sprintf(name, "oet%d_%s%d", tun, valuename, from);
	char *c = nvram_safe_get(name);
	sprintf(name, "oet%d_%s%d", tun, valuename, to);
	nvram_set(name, c);

}

static void copypeertunvalue(char *valuename, int peer, int from, int to)
{
	char name[32];
	sprintf(name, "oet%d_%s%d", from, valuename, peer);
	char *c = nvram_safe_get(name);
	sprintf(name, "oet%d_%s%d", to, valuename, peer);
	nvram_set(name, c);

}

static void delpeervalue(char *valuename, int tun, int from)
{
	char name[32];
	sprintf(name, "oet%d_%s%d", tun, valuename, from);
	nvram_unset(name);
}

static void copypeer(int tun, int from, int to)
{
	copypeervalue("peerkey", tun, from, to);
	copypeervalue("endpoint", tun, from, to);
	copypeervalue("ka", tun, from, to);
	copypeervalue("aip", tun, from, to);
	copypeervalue("peerport", tun, from, to);
	copypeervalue("rem", tun, from, to);
	copypeervalue("usepsk", tun, from, to);
	copypeervalue("psk", tun, from, to);
}

static void copytunpeer(int peer, int from, int to)
{
	copypeertunvalue("peerkey", peer, from, to);
	copypeertunvalue("endpoint", peer, from, to);
	copypeertunvalue("ka", peer, from, to);
	copypeertunvalue("aip", peer, from, to);
	copypeertunvalue("peerport", peer, from, to);
	copypeertunvalue("rem", peer, from, to);
	copypeertunvalue("usepsk", peer, from, to);
	copypeertunvalue("psk", peer, from, to);
}

static void delpeer(int tun, int peer)
{
	delpeervalue("peerkey", tun, peer);
	delpeervalue("ka", tun, peer);
	delpeervalue("endpoint", tun, peer);
	delpeervalue("aip", tun, peer);
	delpeervalue("peerport", tun, peer);
	delpeervalue("rem", tun, peer);
	delpeervalue("usepsk", tun, peer);
	delpeervalue("psk", tun, peer);

}
#endif
static void copytunvalue(char *valuename, int from, int to)
{
	char name[32];
	sprintf(name, "oet%d_%s", from, valuename);
	char *c = nvram_safe_get(name);
	sprintf(name, "oet%d_%s", to, valuename);
	nvram_set(name, c);

}

static void deltunvalue(char *valuename, int tun)
{
	char name[32];
	sprintf(name, "oet%d_%s", tun, valuename);
	nvram_unset(name);
}

void add_tunnel(webs_t wp)
{
	int tunnels = nvram_geti("oet_tunnels");
	tunnels++;
	nvram_seti("oet_tunnels", tunnels);
#define default_set(name,val) if (strlen(nvram_nget("oet%d_%s",tunnels, name))==0)nvram_nset(val, "oet%d_%s",tunnels,name)
	default_set("en", "0");
	default_set("rem", "192.168.90.1");
	default_set("local", "0.0.0.0");
	default_set("ipaddr", "1.2.3.4");
	default_set("netmask", "255.255.255.255");
	default_set("id", "1");
	default_set("proto", "0");
	default_set("bridged", "1");
	default_set("peers", "0");
#undef default_set
}

void del_tunnel(webs_t wp)
{
	int peer;
	char idx[32];
	int tun = websGetVari(wp, "keyindex", -1);
	int tunnels = nvram_geti("oet_tunnels");
	int i;
	for (i = tun + 1; i < tunnels + 1; i++) {
		copytunvalue("en", i, i - 1);
		copytunvalue("rem", i, i - 1);
		copytunvalue("local", i, i - 1);
		copytunvalue("ipaddr", i, i - 1);
		copytunvalue("netmask", i, i - 1);
		copytunvalue("id", i, i - 1);
		copytunvalue("proto", i, i - 1);
		copytunvalue("bridged", i, i - 1);
#ifdef HAVE_WIREGUARD
		copytunvalue("peers", i, i - 1);
		sprintf(idx, "oet%d_peers", i);
		int peers = nvram_geti(idx);
		for (peer = 0; peer < peers; peer++) {
			copytunpeer(peer, i, i - 1);
		}
#endif
	}

#ifdef HAVE_WIREGUARD
	sprintf(idx, "oet%d_peers", tunnels);
	int peers = nvram_geti(idx);
	for (peer = 0; peer < peers; peer++) {
		delpeer(tun, peer);
	}
#endif
	deltunvalue("en", tunnels);
	deltunvalue("rem", tunnels);
	deltunvalue("local", tunnels);
	deltunvalue("ipaddr", tunnels);
	deltunvalue("netmask", tunnels);
	deltunvalue("id", tunnels);
	deltunvalue("proto", tunnels);
	deltunvalue("bridged", tunnels);
#ifdef HAVE_WIREGUARD
	deltunvalue("peers", tunnels);
#endif

	tunnels--;
	nvram_seti("oet_tunnels", tunnels);

}

#ifdef HAVE_WIREGUARD
void del_peer(webs_t wp)
{
	int tun = websGetVari(wp, "keyindex", -1);
	int peer = websGetVari(wp, "peerindex", -1);
	tunnel_save(wp);
	char idx[32];
	int i;
	sprintf(idx, "oet%d_peers", tun);
	int peers = nvram_geti(idx);
	for (i = peer + 1; i < peers; i++) {
		copypeer(tun, i, i - 1);

	}
	delpeer(tun, peers - 1);
	if (peers > 0)
		peers--;
	nvram_seti(idx, peers);
}

void gen_wg_psk(webs_t wp)
{
	tunnel_save(wp);
	int key = websGetVari(wp, "keyindex", -1);
	int peer = websGetVari(wp, "peerindex", -1);
	if (key < 0 || peer < 0)
		return;
	char idx[32];
	sprintf(idx, "%d", key);
	char peeridx[32];
	sprintf(peeridx, "%d", peer);
	eval("makewgpsk", idx, peeridx);
}

#endif
#endif

void qos_save(webs_t wp)
{
	char *value = websGetVar(wp, "action", "");
	char *svqos_var;
	char svqos_pktstr[30] = { 0 };
	char field[32] = { 0 };
	char *name, *data, *level, *level2, *lanlevel, *prio, *delete, *pktopt, *proto;
	int no_svcs = websGetVari(wp, "svqos_nosvcs", 0);
	int no_ips = websGetVari(wp, "svqos_noips", 0);
	int no_devs = websGetVari(wp, "svqos_nodevs", 0);
	int no_macs = websGetVari(wp, "svqos_nomacs", 0);
	int i = 0, j = 0;
	/*
	 * reused wshaper fields - see src/router/rc/wshaper.c 
	 */

	data = websGetVar(wp, "wshaper_enable", NULL);
	nvram_set("wshaper_enable", data);

	if (strcmp(data, "0") == 0) {
		addAction("qos");
		nvram_seti("nowebaction", 1);
		applytake(value);
		return;
	}

	svqos_var = malloc(4096);
	bzero(svqos_var, 4096);

//      nvram_set("enable_game", websGetVar(wp, "enable_game", NULL));
	nvram_seti("svqos_defaults", websGetVari(wp, "svqos_defaults", 0));
	nvram_seti("default_uplevel", websGetVari(wp, "default_uplevel", 0));
	nvram_seti("default_downlevel", websGetVari(wp, "default_downlevel", 0));
	nvram_seti("default_lanlevel", websGetVari(wp, "default_lanlevel", 0));
	nvram_seti("wshaper_downlink", websGetVari(wp, "wshaper_downlink", 0));
	nvram_seti("wshaper_uplink", websGetVari(wp, "wshaper_uplink", 0));
	nvram_set("wshaper_dev", websGetVar(wp, "wshaper_dev", "WAN"));
	nvram_seti("qos_type", websGetVari(wp, "qos_type", 0));

#if defined(HAVE_CODEL) || defined(HAVE_FQ_CODEL) || defined(HAVE_PIE)
	nvram_set("svqos_aqd", websGetVar(wp, "qos_aqd", "sfq"));
#endif

	// nvram_commit ();

	/*
	 * tcp-packet flags
	 */
	bzero(svqos_pktstr, sizeof(svqos_pktstr));

	pktopt = websGetVar(wp, "svqos_pktack", NULL);
	if (pktopt)
		strcat(svqos_pktstr, "ACK | ");
	pktopt = websGetVar(wp, "svqos_pktsyn", NULL);
	if (pktopt)
		strcat(svqos_pktstr, "SYN | ");
	pktopt = websGetVar(wp, "svqos_pktfin", NULL);
	if (pktopt)
		strcat(svqos_pktstr, "FIN | ");
	pktopt = websGetVar(wp, "svqos_pktrst", NULL);
	if (pktopt)
		strcat(svqos_pktstr, "RST | ");

	nvram_set("svqos_pkts", svqos_pktstr);

	/*
	 * services priorities 
	 */

	for (i = 0; i < no_svcs; i++) {
		char protocol[100], ports[100];

		bzero(protocol, 100);
		bzero(ports, 10);

		snprintf(field, sizeof(field), "svqos_svcdel%d", i);
		delete = websGetVar(wp, field, NULL);

		if (delete && strlen(delete) > 0)
			continue;

		snprintf(field, sizeof(field), "svqos_svcname%d", i);
		name = websGetVar(wp, field, NULL);
		if (!name)
			continue;

		snprintf(field, sizeof(field), "svqos_svcprio%d", i);
		level = websGetVar(wp, field, NULL);

		if (!level)
			continue;

		if (get_svc(name, protocol, ports))
			continue;

		if (strcmp(protocol, "l7") == 0) {
			int slen = strlen(name);

			for (j = 0; j < slen; j++)
				name[j] = tolower(name[j]);
		}
#ifdef HAVE_OPENDPI
		if (strcmp(protocol, "dpi") == 0) {
			int slen = strlen(name);

			for (j = 0; j < slen; j++)
				name[j] = tolower(name[j]);
		}
#endif
		if (strlen(svqos_var) > 0) {
			char *tmp;
			asprintf(&tmp, "%s %s %s %s %s |", svqos_var, name, protocol, ports, level);
			strcpy(svqos_var, tmp);
			free(tmp);
		} else
			sprintf(svqos_var, "%s %s %s %s |", name, protocol, ports, level);

	}

	nvram_set("svqos_svcs", svqos_var);
	// nvram_commit ();
	bzero(svqos_var, 4096);

	/*
	 * DEV priorities 
	 */
	for (i = 0; i < no_devs; i++) {

		snprintf(field, sizeof(field), "svqos_devdel%d", i);
		delete = websGetVar(wp, field, NULL);

		if (delete && strlen(delete) > 0)
			continue;

		snprintf(field, sizeof(field), "svqos_dev%d", i);
		data = websGetVar(wp, field, NULL);

		if (!data)
			continue;

#ifndef HAVE_AQOS
		snprintf(field, sizeof(field), "svqos_devprio%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;
		if (strlen(svqos_var) > 0) {
			asprintf(&copy, sizeof(copy), "%s %s %s |", svqos_var, data, level);
			strcpy(svqos_var, copy);
			free(copy);
		} else {
			char *copy;
			asprintf(&copy, "%s %s |", data, level);
			strcpy(svqos_var, copy);
			free(copy);
		}
#else
		snprintf(field, sizeof(field), "svqos_devprio%d", i);
		prio = websGetVar(wp, field, NULL);

		snprintf(field, sizeof(field), "svqos_devup%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;
		snprintf(field, sizeof(field), "svqos_devdown%d", i);
		level2 = websGetVar(wp, field, NULL);
		if (!level2)
			continue;

		snprintf(field, sizeof(field), "svqos_devservice%d", i);
		proto = websGetVar(wp, field, NULL);
		if (!proto)
			continue;

		snprintf(field, sizeof(field), "svqos_devlanlvl%d", i);
		lanlevel = websGetVar(wp, field, NULL);
		if (!lanlevel)
			continue;

		if (strlen(svqos_var) > 0) {
			char *tmp;
			asprintf(&tmp, "%s %s %s %s %s %s %s |", svqos_var, data, level, level2, lanlevel, prio, proto);
			strcpy(svqos_var, tmp);
			free(tmp);
		} else
			sprintf(svqos_var, "%s %s %s %s %s %s |", data, level, level2, lanlevel, prio, proto);

#endif

	}

	nvram_set("svqos_devs", svqos_var);
	bzero(svqos_var, 4096);

	/*
	 * IP priorities 
	 */
	for (i = 0; i < no_ips; i++) {

		snprintf(field, sizeof(field), "svqos_ipdel%d", i);
		delete = websGetVar(wp, field, NULL);

		if (delete && strlen(delete) > 0)
			continue;

		snprintf(field, sizeof(field), "svqos_ip%d", i);
		data = websGetVar(wp, field, NULL);
		if (!data)
			continue;

#ifndef HAVE_AQOS
		snprintf(field, sizeof(field), "svqos_ipprio%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;
		if (strlen(svqos_var) > 0)
			sprintf(svqos_var, "%s %s %s |", svqos_var, data, level);
		else
			sprintf(svqos_var, "%s %s |", data, level);
#else
		snprintf(field, sizeof(field), "svqos_ipprio%d", i);
		prio = websGetVar(wp, field, NULL);
		if (!prio)
			continue;

		snprintf(field, sizeof(field), "svqos_ipup%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;
		snprintf(field, sizeof(field), "svqos_ipdown%d", i);
		level2 = websGetVar(wp, field, NULL);
		if (!level2)
			continue;
		snprintf(field, sizeof(field), "svqos_iplanlvl%d", i);
		lanlevel = websGetVar(wp, field, NULL);
		if (!lanlevel)
			continue;

		if (strlen(svqos_var) > 0) {
			char *tmp;
			asprintf(&tmp, "%s %s %s %s %s %s |", svqos_var, data, level, level2, lanlevel, prio);
			strcpy(svqos_var, tmp);
			free(tmp);
		} else
			sprintf(svqos_var, "%s %s %s %s %s |", data, level, level2, lanlevel, prio);

#endif

	}

	nvram_set("svqos_ips", svqos_var);
	// nvram_commit ();
	bzero(svqos_var, 4096);

	/*
	 * MAC priorities 
	 */
	for (i = 0; i < no_macs; i++) {
		snprintf(field, sizeof(field), "svqos_macdel%d", i);
		delete = websGetVar(wp, field, NULL);

		if (delete && strlen(delete) > 0)
			continue;

		snprintf(field, sizeof(field), "svqos_mac%d", i);
		data = websGetVar(wp, field, NULL);
		if (!data)
			continue;

#ifndef HAVE_AQOS
		snprintf(field, sizeof(field), "svqos_macprio%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;

		if (strlen(svqos_var) > 0)
			sprintf(svqos_var, "%s %s %s |", svqos_var, data, level);
		else
			sprintf(svqos_var, "%s %s |", data, level);
#else
		snprintf(field, sizeof(field), "svqos_macprio%d", i);
		prio = websGetVar(wp, field, NULL);
		if (!prio)
			continue;

		snprintf(field, sizeof(field), "svqos_macup%d", i);
		level = websGetVar(wp, field, NULL);
		if (!level)
			continue;
		snprintf(field, sizeof(field), "svqos_macdown%d", i);
		level2 = websGetVar(wp, field, NULL);
		if (!level2)
			continue;
		snprintf(field, sizeof(field), "svqos_maclanlvl%d", i);
		lanlevel = websGetVar(wp, field, NULL);
		if (!lanlevel)
			continue;

		if (strlen(svqos_var) > 0) {
			char *tmp;
			asprintf(&tmp, "%s %s %s %s user %s %s |", svqos_var, data, level, level2, lanlevel, prio);
			strcpy(svqos_var, tmp);
			free(tmp);
		} else
			sprintf(svqos_var, "%s %s %s user %s %s |", data, level, level2, lanlevel, prio);

#endif

	}

	nvram_set("svqos_macs", svqos_var);
	free(svqos_var);
	// nvram_commit ();

	/*
	 * adm6996 LAN port priorities 
	 */
	nvram_seti("svqos_port1prio", websGetVari(wp, "svqos_port1prio", 0));
	nvram_seti("svqos_port2prio", websGetVari(wp, "svqos_port2prio", 0));
	nvram_seti("svqos_port3prio", websGetVari(wp, "svqos_port3prio", 0));
	nvram_seti("svqos_port4prio", websGetVari(wp, "svqos_port4prio", 0));

	nvram_seti("svqos_port1bw", websGetVari(wp, "svqos_port1bw", 0));
	nvram_seti("svqos_port2bw", websGetVari(wp, "svqos_port2bw", 0));
	nvram_seti("svqos_port3bw", websGetVari(wp, "svqos_port3bw", 0));
	nvram_seti("svqos_port4bw", websGetVari(wp, "svqos_port4bw", 0));

	addAction("qos");
	nvram_seti("nowebaction", 1);
	applytake(value);

}

static void macro_add(char *a)
{
	cprintf("adding %s\n", a);

	char *count;
	int c;
	char buf[20];

	count = nvram_safe_get(a);
	cprintf("count = %s\n", count);
	if (count != NULL && strlen(count) > 0) {
		c = atoi(count);
		if (c > -1) {
			c++;
			sprintf(buf, "%d", c);
			cprintf("set %s to %s\n", a, buf);
			nvram_set(a, buf);
		}
	}
	return;
}

static void macro_rem(char *a, char *nv)
{
	char *count;
	int c, i, cnt;
	char buf[20];
	char *buffer, *b;

	cnt = 0;
	count = nvram_safe_get(a);
	if (count != NULL && strlen(count) > 0) {
		c = atoi(count);
		if (c > 0) {
			c--;
			sprintf(buf, "%d", c);
			nvram_set(a, buf);
			buffer = nvram_safe_get(nv);
			if (buffer != NULL) {
				int slen = strlen(buffer);

				b = safe_malloc(slen + 1);

				for (i = 0; i < slen; i++) {
					if (buffer[i] == ' ')
						cnt++;
					if (cnt == c)
						break;
					b[i] = buffer[i];
				}
				b[i] = 0;
				nvram_set(nv, b);
				free(b);
			}

		}
	}
	return;
}

void forward_remove(webs_t wp)
{
	macro_rem("forward_entries", "forward_port");
}

void forward_add(webs_t wp)
{
	macro_add("forward_entries");
}

void filter_remove(webs_t wp)
{
	char filter[32];
	sprintf(filter, "numfilterservice%d", wp->p->filter_id);
	int numfilters = nvram_default_geti(filter, 4);
	if (numfilters > 0)
		numfilters--;
	char num[32];
	sprintf(num, "%d", numfilters);
	nvram_set(filter, num);
}

void filter_add(webs_t wp)
{
	char filter[32];
	sprintf(filter, "numfilterservice%d", wp->p->filter_id);
	int numfilters = nvram_default_geti(filter, 4);
	numfilters++;
	char num[32];
	sprintf(num, "%d", numfilters);
	nvram_set(filter, num);
}

void lease_remove(webs_t wp)
{
	macro_rem("static_leasenum", "static_leases");
}

void lease_add(webs_t wp)
{
	macro_add("static_leasenum");
}

#ifdef HAVE_PPPOESERVER
void chap_user_add(webs_t wp)
{
	int var = websGetVari(wp, "pppoeserver_enabled", 0);

	nvram_seti("pppoeserver_enabled", var);
	macro_add("pppoeserver_chapsnum");
}

void chap_user_remove(webs_t wp)
{
	int var = websGetVari(wp, "pppoeserver_enabled", 0);

	nvram_seti("pppoeserver_enabled", var);
	macro_rem("pppoeserver_chapsnum", "pppoeserver_chaps");
}
#endif

#ifdef HAVE_MILKFISH
void milkfish_user_add(webs_t wp)
{
	macro_add("milkfish_ddsubscribersnum");
}

void milkfish_user_remove(webs_t wp)
{
	macro_rem("milkfish_ddsubscribersnum", "milkfish_ddsubscribers");
}

void milkfish_alias_add(webs_t wp)
{
	macro_add("milkfish_ddaliasesnum");
}

void milkfish_alias_remove(webs_t wp)
{
	macro_rem("milkfish_ddaliasesnum", "milkfish_ddaliases");
}
#endif

void forwardspec_remove(webs_t wp)
{
	macro_rem("forwardspec_entries", "forward_spec");
}

void forwardspec_add(webs_t wp)
{
	macro_add("forwardspec_entries");
}

void trigger_remove(webs_t wp)
{
	macro_rem("trigger_entries", "port_trigger");
}

void trigger_add(webs_t wp)
{
	macro_add("trigger_entries");
}

int get_vifcount(char *prefix)
{
	char *next;
	char var[80];
	char wif[16];

	sprintf(wif, "%s_vifs", prefix);
	char *vifs = nvram_safe_get(wif);

	if (vifs == NULL)
		return 0;
	int count = 0;

	foreach(var, vifs, next) {
		count++;
	}
	return count;
}

#ifdef HAVE_GUESTPORT
int gp_action = 0;

void add_mdhcpd(char *iface, int start, int max, int leasetime)
{

	char mdhcpd[32];
	char *mdhcpds;
	int var[8];

	// add mdhcpd
	if (nvram_geti("mdhcpd_count") > 0)
		sprintf(mdhcpd, " %s>On>%d>%d>%d", iface, start, max, leasetime);
	else
		sprintf(mdhcpd, "%s>On>%d>%d>%d", iface, start, max, leasetime);
	mdhcpds = safe_malloc(strlen(nvram_safe_get("mdhcpd")) + strlen(mdhcpd) + 2);
	sprintf(mdhcpds, "%s%s", nvram_safe_get("mdhcpd"), mdhcpd);
	nvram_set("mdhcpd", mdhcpds);
	free(mdhcpds);

	sprintf(var, "%d", nvram_geti("mdhcpd_count") + 1);
	nvram_set("mdhcpd_count", var);
}

void remove_mdhcp(char *iface)
{

	char *start, *next, *pref, *suff;
	char *mdhcpds = safe_malloc(strlen(nvram_safe_get("mdhcpd")) + 1);
	int len;
	char var[4];

	strcpy(mdhcpds, nvram_safe_get("mdhcpd"));
	start = strstr(mdhcpds, iface);
	//fprintf(stderr, "checking.... %s -> %s %s\n", mdhcpds, iface, start);
	if (start) {
		len = strlen(mdhcpds) - strlen(start);
		if (len > 0) {
			pref = safe_malloc(len);
			strncpy(pref, mdhcpds, len - 1);
			pref[len - 1] = '\0';
		} else {
			pref = safe_malloc(1);
			pref[0] = '\0';
		}
		//fprintf(stderr, "[PREF] %s\n", pref);

		next = strchr(start, ' ');
		if (next) {
			// cut entry
			len = strlen(next);
			suff = strdup(next);
			suff[len - 1] = '\0';
		} else {
			// entry at the end?
			suff = safe_malloc(1);
			suff[0] = '\0';
		}

		free(mdhcpds);

		//fprintf(stderr, "[PREF/SUFF] %s %s\n", pref, suff);   
		len = strlen(pref) + strlen(suff);
		mdhcpds = safe_malloc(len + 2);
		sprintf(mdhcpds, "%s %s", pref, suff);
		mdhcpds[len + 1] = '\0';
		//fprintf(stderr, "[MDHCP] %s\n", mdhcpds);
		nvram_set("mdhcpd", mdhcpds);

		len = nvram_geti("mdhcpd_count");
		if (len > 0) {
			len--;
			//fprintf(stderr, "[MDHCPDS] %d\n", len);
			sprintf(var, "%d", len);
			nvram_set("mdhcpd_count", var);
		}

		free(mdhcpds);
		free(pref);
		free(suff);
	}
}

void move_mdhcp(char *siface, char *tiface)
{

	char *start;
	char *mdhcpds = NULL;
	int i, len, pos;
	char iface[16];
	mdhcpds = strdup(nvram_safe_get("mdhcpd"));
	start = strstr(mdhcpds, siface);
	if (start) {
		strcpy(iface, tiface);
		len = strlen(tiface);
		pos = strlen(mdhcpds) - strlen(start);
		for (i = 0; i < len; i++) {
			mdhcpds[pos + i] = iface[i];
		}
		//fprintf(stderr, "[MDHCPD] %s->%s %d %s\n", siface, tiface, pos, mdhcpds);
		nvram_set("mdhcpd", mdhcpds);
		free(mdhcpds);
	}
}

char *getFreeLocalIpNet()
{
	return "192.168.12.1";
}
#endif
void add_vifs_single(char *prefix, int device)
{
	int count = get_vifcount(prefix);

	if (count == 16)
		return;
	char vif[16];

	sprintf(vif, "%s_vifs", prefix);
	char *vifs = nvram_safe_get(vif);

	if (vifs == NULL)
		return;
	char *n = (char *)safe_malloc(strlen(vifs) + 16);
	char v[80];
	char v2[80];
#ifdef HAVE_GUESTPORT
	char guestport[16];
	sprintf(guestport, "guestport_%s", prefix);
#endif

#ifdef HAVE_MADWIFI
	// char *cou[] = { "a", "b", "c", "d", "e", "f" };
	sprintf(v, "ath%d.%d", device, count + 1);
#else
	sprintf(v, "wl%d.%d", device, count + 1);
#endif
	if (!strlen(vifs))
		sprintf(n, "%s", v);
	else
		sprintf(n, "%s %s", vifs, v);
	sprintf(v2, "%s_closed", v);
	nvram_seti(v2, 0);
	sprintf(v2, "%s_mode", v);
	nvram_set(v2, "ap");

	sprintf(v2, "%s_ap_isolate", v);
	nvram_seti(v2, 0);
	sprintf(v2, "%s_ssid", v);
#ifdef HAVE_MAKSAT
#ifdef HAVE_MAKSAT_BLANK
	nvram_set(v2, "default_vap");
#else
	nvram_set(v2, "maksat_vap");
#endif
#elif defined(HAVE_SANSFIL)
	nvram_set(v2, "sansfil_vap");
#elif defined(HAVE_TRIMAX)
	nvram_set(v2, "m2m_vap");
#elif defined(HAVE_WIKINGS)
	nvram_set(v2, "Excel Networks_vap");
#elif defined(HAVE_ESPOD)
	nvram_set(v2, "ESPOD Technologies_vap");
#elif defined(HAVE_NEXTMEDIA)
	nvram_set(v2, "nextmedia_vap");
#elif defined(HAVE_TMK)
	nvram_set(v2, "KMT_vap");
#elif defined(HAVE_CORENET)
	nvram_set(v2, "corenet.ap");
#elif defined(HAVE_ONNET)
	nvram_set(v2, "OTAi_vap");
#elif defined(HAVE_KORENRON)
	nvram_set(v2, "WBR2000_vap");
#elif defined(HAVE_HDWIFI)
	nvram_set(v2, "hdwifi_vap");
#elif defined(HAVE_RAYTRONIK)
	nvram_set(v2, "RN-150M");
#elif defined(HAVE_HOBBIT)
	nvram_set(v2, "hobb-it_vap");
#elif defined(HAVE_ANTAIRA)
	nvram_set(v2, "antaira_vap");
#else
	nvram_set(v2, "dd-wrt_vap");
#endif
	sprintf(v2, "%s_vifs", prefix);
	nvram_set(v2, n);
	sprintf(v2, "%s_bridged", v);
	nvram_seti(v2, 1);
	sprintf(v2, "%s_nat", v);
	nvram_seti(v2, 1);
	sprintf(v2, "%s_ipaddr", v);
	nvram_set(v2, "0.0.0.0");
	sprintf(v2, "%s_netmask", v);
	nvram_set(v2, "0.0.0.0");

	sprintf(v2, "%s_gtk_rekey", v);
	nvram_seti(v2, 3600);

	sprintf(v2, "%s_radius_port", v);
	nvram_seti(v2, 1812);

	sprintf(v2, "%s_radius_ipaddr", v);
	nvram_set(v2, "0.0.0.0");

#ifdef HAVE_MADWIFI

	sprintf(v2, "%s_radius2_ipaddr", v);
	nvram_set(v2, "0.0.0.0");

	sprintf(v2, "%s_radius2_port", v);
	nvram_seti(v2, 1812);

	sprintf(v2, "%s_local_ip", v);
	nvram_set(v2, "0.0.0.0");
#endif
#ifdef HAVE_GUESTPORT
	char v3[80];
	if (gp_action == 1) {
		nvram_set(guestport, v);

		sprintf(v2, "%s_ssid", v);
#ifdef HAVE_WZRHPAG300NH
		if (has_5ghz(prefix))
			nvram_set(v2, "GuestPort_A");
		else
			nvram_set(v2, "GuestPort_G");
#else
		nvram_set(v2, "GuestPort");
#endif

		sprintf(v2, "%s_bridged", v);
		nvram_seti(v2, 0);

		sprintf(v2, "%s_ipaddr", v);
		nvram_set(v2, getFreeLocalIpNet());

		sprintf(v2, "%s_netmask", v);
		nvram_set(v2, "255.255.255.0");

		sprintf(v2, "%s_security_mode", v);
		nvram_set(v2, "psk psk2");

		sprintf(v2, "%s_akm", v);
		nvram_set(v2, "psk psk2");

#ifdef HAVE_MADWIFI
		sprintf(v2, "%s_tkip", v);
		nvram_set(v2, "1");
		sprintf(v2, "%s_aes", v);
		nvram_set(v2, "1");
#else
		sprintf(v2, "%s_crypto", v);
		nvram_set(v2, "tkip+aes");
#endif
		sprintf(v2, "%s_wpa_psk", v);
#ifdef HAVE_WZRHPAG300NH
		if (has_5ghz(prefix))
			sprintf(v3, "DEF-p_wireless_%s0_11a-wpapsk", prefix);
		else
			sprintf(v3, "DEF-p_wireless_%s0_11bg-wpapsk", prefix);
#else
		sprintf(v3, "DEF-p_wireless_%s_11bg-wpapsk", prefix);
#endif
		nvram_set(v2, getUEnv(v3));

		add_mdhcpd(v, 20, 200, 1440);
		//required to use mdhcpd
		nvram_seti("dhcp_dnsmasq", 1);

		rep(v, '.', 'X');

		sprintf(v2, "%s_security_mode", v);
		nvram_set(v2, "psk psk2");
	}
	gp_action = 0;
#endif

	// nvram_commit ();
	free(n);
}

void add_vifs(webs_t wp)
{
	char *prefix = websGetVar(wp, "iface", NULL);

	if (prefix == NULL)
		return;
	int devcount = prefix[strlen(prefix) - 1] - '0';
#ifdef HAVE_GUESTPORT
	if (!strcmp(websGetVar(wp, "gp_modify", ""), "add")) {
		gp_action = 1;
	}
#endif
	add_vifs_single(prefix, devcount);
}

#ifdef HAVE_GUESTPORT
void move_vif(char *prefix, char *svif, char *tvif)
{

	char command[64];

	sprintf(command, "nvram show | grep %s_", svif);

	FILE *fp;
	char line[80];
	char var[16];
	char tvifx[16];
	char nvram_var[32];
	char nvram_val[32];
	int len;
	int pos = 0;
	int xpos;

	strcpy(tvifx, tvif);
	rep(tvifx, '.', 'X');

	if ((fp = popen(command, "r"))) {
		while (fgets(line, sizeof(line), fp)) {
			pos = strcspn(line, "=");
			if (pos) {
				xpos = strcspn(line, "X");
				len = strlen(svif);
				strncpy(var, line + len, pos - len);
				var[pos - len] = '\0';
				if (xpos > 0 && xpos < pos) {
					sprintf(nvram_var, "%s%s", tvifx, var);
				} else {
					sprintf(nvram_var, "%s%s", tvif, var);
				}

				strncpy(nvram_val, line + pos + 1, strlen(line) - pos);
				nvram_val[strlen(line) - pos - 2] = '\0';
				//fprintf(stderr, "[VIF] %s %s\n", nvram_var, nvram_val);
				nvram_set(nvram_var, nvram_val);
			}
		}
		pclose(fp);
	}
}
#endif
void remove_vifs_single(char *prefix)
{
	char wif[16];

	sprintf(wif, "%s_vifs", prefix);
	int o = -1;
	char *vifs = nvram_safe_get(wif);
	char *copy = strdup(vifs);
	int i;
	int slen = strlen(copy);

#ifdef HAVE_GUESTPORT
	int q = 0;
	int j;
	int gp_found = 0;
	char vif[16], pvif[16];
	char guestport[16];
	sprintf(guestport, "guestport_%s", prefix);

	if (nvram_get(guestport)) {
		if (gp_action == 2) {
			for (i = 0; i <= slen; i++) {
				if (copy[i] == 0x20 || i == slen) {
					if (gp_found)
						strcpy(pvif, vif);
					if (o > 0)
						q = o + 1;
					o = i;
					for (j = 0; j < o - q; j++) {
						vif[j] = copy[j + q];
					}
					vif[j] = '\0';

					if (gp_found) {
						move_vif(prefix, vif, pvif);
					}

					if (nvram_match(guestport, vif))
						gp_found = 1;
				}
			}
			remove_mdhcp(nvram_get(guestport));
			nvram_unset(guestport);
		} else {
			o = slen;
			for (i = slen; i >= 0; i--) {
				if (copy[i] == 0x20 || i == 0) {
					if (gp_found)
						strcpy(pvif, vif);
					if (i == 0)
						q = i;
					else
						q = i + 1;
					for (j = 0; j < o - q; j++) {
						vif[j] = copy[j + q];
					}
					vif[j] = '\0';

					if (gp_found == slen) {
						move_vif(prefix, pvif, vif);
						nvram_set(guestport, vif);
						move_mdhcp(pvif, vif);
						gp_found = 0;
					}

					if (nvram_match(guestport, vif))
						gp_found = o;
					o = i;
				}
			}
		}
	}
	gp_action = 0;
#endif
	o = -1;
	for (i = 0; i < slen; i++) {
		if (copy[i] == 0x20)
			o = i;
	}

	if (o == -1) {
		nvram_set(wif, "");
	} else {
		copy[o] = 0;
		nvram_set(wif, copy);
	}
	// nvram_commit ();
#ifdef HAVE_AOSS
// must remove all aoss vap's if one of them is touched
	if (strlen(nvram_safe_get("aoss_vifs"))) {
		nvram_unset("ath0_vifs");
		nvram_unset("aoss_vifs");
		nvram_commit();
	}
	if (strlen(nvram_safe_get("aossa_vifs"))) {
		nvram_unset("ath1_vifs");
		nvram_unset("aossa_vifs");
		nvram_commit();
	}
#endif
	free(copy);
}

void remove_vifs(webs_t wp)
{
	char *prefix = websGetVar(wp, "iface", NULL);
#ifdef HAVE_GUESTPORT
	if (!strcmp(websGetVar(wp, "gp_modify", ""), "remove")) {
		gp_action = 2;
	}
#endif
	remove_vifs_single(prefix);
}

#ifdef HAVE_BONDING
void add_bond(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("bonding_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("bondings");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("bonding_count", var);
	nvram_commit();
	return;
}

void del_bond(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("bondings");
	newwordlist = (char *)calloc(strlen(wordlist) + 2, 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		}
		count++;
	}

	char var[32];

	realcount = nvram_geti("bonding_count") - 1;
	sprintf(var, "%d", realcount);
	nvram_set("bonding_count", var);
	nvram_set("bondings", newwordlist);
	nvram_commit();
	free(newwordlist);

	return;
}
#endif

#ifdef HAVE_OLSRD
void add_olsrd(webs_t wp)
{
	char *ifname = websGetVar(wp, "olsrd_ifname", NULL);

	if (ifname == NULL)
		return;
	char *wordlist = nvram_safe_get("olsrd_interfaces");
	char *addition = ">5.0>90.0>2.0>270.0>15.0>90.0>15.0>90.0";
	char *newadd = (char *)safe_malloc(strlen(wordlist) + strlen(addition) + strlen(ifname) + 2);
	if (strlen(wordlist) > 0) {
		strcpy(newadd, wordlist);
		strcat(newadd, " ");
		strcat(newadd, ifname);
	} else {
		strcpy(newadd, ifname);
	}
	strcat(newadd, addition);
	nvram_set("olsrd_interfaces", newadd);
	nvram_commit();
	free(newadd);
	return;
}

void del_olsrd(webs_t wp)
{
	int d = websGetVari(wp, "olsrd_delcount", -1);

	char *wordlist = nvram_safe_get("olsrd_interfaces");
	char *newlist = (char *)calloc(strlen(wordlist) + 2, 1);

	char *next;
	char word[128];
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != d) {
			strcat(newlist, " ");
			strcat(newlist, word);
		}
		count++;
	}
	nvram_set("olsrd_interfaces", newlist);
	nvram_commit();
	free(newlist);
	return;
}

void save_olsrd(webs_t wp)
{
	char *wordlist = nvram_safe_get("olsrd_interfaces");
	char *newlist = (char *)calloc(strlen(wordlist) + 512, 1);

	char *next;
	char word[64];

	foreach(word, wordlist, next) {
		char *interface = word;
		char *dummy = interface;

		strsep(&dummy, ">");
		char valuename[32];

		sprintf(valuename, "%s_hellointerval", interface);
		char *hellointerval = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_hellovaliditytime", interface);
		char *hellovaliditytime = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_tcinterval", interface);
		char *tcinterval = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_tcvaliditytime", interface);
		char *tcvaliditytime = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_midinterval", interface);
		char *midinterval = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_midvaliditytime", interface);
		char *midvaliditytime = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_hnainterval", interface);
		char *hnainterval = websGetVar(wp, valuename, "0");

		sprintf(valuename, "%s_hnavaliditytime", interface);
		char *hnavaliditytime = websGetVar(wp, valuename, "0");
		char *tmp;
		asprintf(&tmp, "%s %s>%s>%s>%s>%s>%s>%s>%s>%s", newlist, interface, hellointerval, hellovaliditytime, tcinterval, tcvaliditytime, midinterval, midvaliditytime, hnainterval, hnavaliditytime);
		strcpy(newlist, tmp);
		free(tmp);
	}
	nvram_set("olsrd_interfaces", newlist);
	nvram_commit();
	free(newlist);
	return;
}
#endif

#ifdef HAVE_STATUS_GPIO
void gpios_save(webs_t wp)
{
	char *var, *next;
	char nvgpio[32], gpioname[32];

	char *value = websGetVar(wp, "action", "");

	char *gpios = nvram_safe_get("gpio_outputs");
	var = (char *)malloc(strlen(gpios) + 1);
	if (var != NULL) {
		if (gpios != NULL) {
			foreach(var, gpios, next) {
				sprintf(nvgpio, "gpio%s", var);
				sprintf(gpioname, "gpio%s_name", var);
				value = websGetVar(wp, nvgpio, NULL);
				if (value) {
					set_gpio(atoi(var), atoi(value));
					nvram_set(nvgpio, value);
				}
				value = websGetVar(wp, gpioname, NULL);
				if (value) {
					nvram_set(gpioname, value);
				} else {
					nvram_set(gpioname, "");
				}
			}
		}
		free(var);
	}
	gpios = nvram_safe_get("gpio_inputs");
	var = (char *)malloc(strlen(gpios) + 1);
	if (var != NULL) {
		if (gpios != NULL) {
			foreach(var, gpios, next) {
				sprintf(gpioname, "gpio%s_name", var);
				value = websGetVar(wp, gpioname, NULL);
				if (value) {
					nvram_set(gpioname, value);
				} else {
					nvram_set(gpioname, "");
				}

			}
		}
		free(var);
	}
	applytake(value);
}
#endif
#ifdef HAVE_VLANTAGGING

static void trunkspaces(char *str)
{
	int len = strlen(str);
	int i;
	for (i = 0; i < len; i++) {
		if (str[i] == ' ') {
			memmove(&str[i], &str[i + 1], len - i);
			i--;
			len = strlen(str);
			continue;
		}
	}
}

void save_networking(webs_t wp)
{
	char *value = websGetVar(wp, "action", "");
	int vlancount = nvram_geti("vlan_tagcount");
	int bridgescount = nvram_geti("bridges_count");
	int bridgesifcount = nvram_geti("bridgesif_count");
	int mdhcpd_count = nvram_geti("mdhcpd_count");
#ifdef HAVE_IPVS
	int ipvscount = nvram_geti("ipvs_count");
	int ipvstargetcount = nvram_geti("ipvstarget_count");
#endif
#ifdef HAVE_BONDING
	int bondcount = nvram_geti("bonding_count");
#endif
	int i;

	// save vlan stuff
	char buffer[1024];

	bzero(buffer, 1024);
	for (i = 0; i < vlancount; i++) {
		char *ifname, *tag, *prio;
		char var[32];

		sprintf(var, "vlanifname%d", i);
		ifname = websGetVar(wp, var, NULL);
		if (!ifname)
			break;
		sprintf(var, "vlantag%d", i);
		tag = websGetVar(wp, var, NULL);
		if (!tag)
			break;
		sprintf(var, "vlanprio%d", i);
		prio = websGetVar(wp, var, NULL);
		if (!prio)
			break;
		strcat(buffer, ifname);
		strcat(buffer, ">");
		strcat(buffer, tag);
		strcat(buffer, ">");
		strcat(buffer, prio);
		if (i < vlancount - 1)
			strcat(buffer, " ");
	}
	nvram_set("vlan_tags", buffer);
	// save bonds
	bzero(buffer, 1024);
#ifdef HAVE_BONDING
	char *bondingnumber = websGetVar(wp, "bonding_number", NULL);

	if (bondingnumber)
		nvram_set("bonding_number", bondingnumber);
	char *bondingtype = websGetVar(wp, "bonding_type", NULL);

	if (bondingtype)
		nvram_set("bonding_type", bondingtype);
	for (i = 0; i < bondcount; i++) {
		char *ifname, *tag;
		char var[32];

		sprintf(var, "bondingifname%d", i);
		ifname = websGetVar(wp, var, NULL);
		if (!ifname)
			break;
		sprintf(var, "bondingattach%d", i);
		tag = websGetVar(wp, var, NULL);
		if (!tag)
			break;
		strcat(buffer, ifname);
		strcat(buffer, ">");
		strcat(buffer, tag);
		if (i < bondcount - 1)
			strcat(buffer, " ");
	}
	nvram_set("bondings", buffer);
	bzero(buffer, 1024);
#endif
#ifdef HAVE_IPVS
	{
		char var[32];
		sprintf(var, "ipvsrole");
		char *ipvsrole = websGetVar(wp, var, NULL);
		if (ipvsrole) {
			if (!strcmp(ipvsrole, "Master"))
				nvram_set("ipvs_role", "master");
			else
				nvram_set("ipvs_role", "backup");
		}

	}
	for (i = 0; i < ipvscount; i++) {
		char *ipvsname;
		char *ipvsip;
		char *ipvsport;
		char *ipvsproto;
		char *ipvsscheduler;
		char var[32];
		sprintf(var, "ipvsname%d", i);
		ipvsname = websGetVar(wp, var, NULL);
		if (!ipvsname)
			break;
		trunkspaces(ipvsname);

		sprintf(var, "ipvsip%d", i);
		ipvsip = websGetVar(wp, var, NULL);
		if (!ipvsip)
			break;
		trunkspaces(ipvsip);

		sprintf(var, "ipvsport%d", i);
		ipvsport = websGetVar(wp, var, NULL);
		if (!ipvsport)
			break;
		trunkspaces(ipvsport);

		sprintf(var, "ipvsscheduler%d", i);
		ipvsscheduler = websGetVar(wp, var, NULL);
		if (!ipvsscheduler)
			break;
		trunkspaces(ipvsscheduler);

		sprintf(var, "ipvsproto%d", i);
		ipvsproto = websGetVar(wp, var, NULL);
		if (!ipvsproto)
			break;
		trunkspaces(ipvsproto);

		strcat(buffer, ipvsname);
		strcat(buffer, ">");
		strcat(buffer, ipvsip);
		strcat(buffer, ">");
		strcat(buffer, ipvsport);
		strcat(buffer, ">");
		strcat(buffer, ipvsscheduler);
		strcat(buffer, ">");
		strcat(buffer, ipvsproto);
		if (i < ipvscount - 1)
			strcat(buffer, " ");
	}
	nvram_set("ipvs", buffer);
	bzero(buffer, 1024);

	for (i = 0; i < ipvstargetcount; i++) {
		char *ipvsname;
		char *ipvsip;
		char *ipvsport;
		char *ipvsweight;
		char *ipvsnat;
		char var[32];
		sprintf(var, "target_ipvsname%d", i);
		ipvsname = websGetVar(wp, var, NULL);
		if (!ipvsname)
			break;
		trunkspaces(ipvsname);

		sprintf(var, "target_ipvsip%d", i);
		ipvsip = websGetVar(wp, var, NULL);
		if (!ipvsip)
			break;
		trunkspaces(ipvsip);

		sprintf(var, "target_ipvsport%d", i);
		ipvsport = websGetVar(wp, var, NULL);
		if (!ipvsport)
			break;
		trunkspaces(ipvsport);

		sprintf(var, "target_ipvsweight%d", i);
		ipvsweight = websGetVar(wp, var, NULL);
		if (!ipvsweight)
			break;
		trunkspaces(ipvsweight);
		sprintf(var, "target_ipvsmasquerade%d", i);
		ipvsnat = websGetVar(wp, var, "0");
		if (!ipvsnat)
			break;
		trunkspaces(ipvsnat);

		strcat(buffer, ipvsname);
		strcat(buffer, ">");
		strcat(buffer, ipvsip);
		strcat(buffer, ">");
		strcat(buffer, ipvsport);
		strcat(buffer, ">");
		strcat(buffer, ipvsweight);
		strcat(buffer, ">");
		strcat(buffer, ipvsnat);
		if (i < ipvstargetcount - 1)
			strcat(buffer, " ");
	}
	nvram_set("ipvstarget", buffer);
	bzero(buffer, 1024);

#endif

	// save bridges

	for (i = 0; i < bridgescount; i++) {
		char *ifname, *tag, *prio, *mtu, *mcast;
		char var[32];
		char ipaddr[32];
		char netmask[32];
		char n[32];

		bzero(ipaddr, 32);
		bzero(netmask, 32);
		sprintf(var, "bridgename%d", i);
		ifname = websGetVar(wp, var, NULL);
		if (!ifname)
			break;
		sprintf(var, "bridgestp%d", i);
		tag = websGetVar(wp, var, NULL);
		if (!tag)
			break;
		sprintf(var, "bridgemcastbr%d", i);
		mcast = websGetVar(wp, var, NULL);
		if (!mcast) {
			break;
		} else {
			sprintf(n, "%s_mcast", ifname);
			if (!strcmp(mcast, "On"))
				nvram_seti(n, 1);
			else
				nvram_seti(n, 0);
		}
		sprintf(var, "bridgeprio%d", i);
		prio = websGetVar(wp, var, "32768");
		if (strlen(prio) == 0)
			prio = "32768";

		if (atoi(prio) > 61440)
			prio = "61440";

		sprintf(var, "bridgemtu%d", i);
		mtu = websGetVar(wp, var, NULL);
		if (!mtu)
			mtu = "1500";
		if (strlen(mtu) == 0)
			mtu = "1500";

		copymergetonv(wp, "%s_ipaddr", ifname);

		copymergetonv(wp, "%s_netmask", ifname);

		strcat(buffer, ifname);
		strcat(buffer, ">");
		strcat(buffer, tag);
		strcat(buffer, ">");
		strcat(buffer, prio);
		strcat(buffer, ">");
		strcat(buffer, mtu);
		if (i < bridgescount - 1)
			strcat(buffer, " ");

		char brname[32];

		if (!strcmp(ifname, "br0"))
			sprintf(brname, "lan_hwaddr");
		else
			sprintf(brname, "%s_hwaddr", ifname);

		nvram_set(brname, websGetVar(wp, brname, NULL));
	}
	nvram_set("bridges", buffer);
	// save bridge assignment
	bzero(buffer, 1024);
	for (i = 0; i < bridgesifcount; i++) {
		char *ifname, *tag, *prio, *hairpin, *stp, *cost;
		char var[32];

		sprintf(var, "bridge%d", i);
		ifname = websGetVar(wp, var, NULL);
		if (!ifname)
			break;
		sprintf(var, "bridgeif%d", i);
		tag = websGetVar(wp, var, NULL);
		if (!tag)
			break;
		sprintf(var, "bridgeifstp%d", i);
		stp = websGetVar(wp, var, "On");
		if (!strcmp(stp, "On"))
			stp = "1";
		else
			stp = "0";
		sprintf(var, "bridgeifprio%d", i);
		prio = websGetVar(wp, var, "112");
		if (strlen(prio) == 0)
			prio = "112";

		if (atoi(prio) > 240)
			prio = "240";

		sprintf(var, "bridgeifhairpin%d", i);
		hairpin = websGetVar(wp, var, "0");

		sprintf(var, "bridgeifcost%d", i);
		cost = websGetVar(wp, var, "100");
		if (strlen(cost) == 0)
			prio = "100";

		strcat(buffer, ifname);
		strcat(buffer, ">");
		strcat(buffer, tag);
		strcat(buffer, ">");
		strcat(buffer, prio);
		strcat(buffer, ">");
		strcat(buffer, hairpin);
		strcat(buffer, ">");
		strcat(buffer, stp);
		strcat(buffer, ">");
		strcat(buffer, cost);
		if (i < bridgesifcount - 1)
			strcat(buffer, " ");
	}
	nvram_set("bridgesif", buffer);
#ifdef HAVE_MDHCP
	// save multipe dhcp-servers
	bzero(buffer, 1024);
	// if (!interface || !start || !dhcpon || !max || !leasetime)
	for (i = 0; i < mdhcpd_count; i++) {
		char *mdhcpinterface, *mdhcpon, *mdhcpstart, *mdhcpmax, *mdhcpleasetime;
		char var[32];

		sprintf(var, "mdhcpifname%d", i);
		mdhcpinterface = websGetVar(wp, var, NULL);
		if (!mdhcpinterface)
			break;

		sprintf(var, "mdhcpon%d", i);
		mdhcpon = websGetVar(wp, var, NULL);
		if (!mdhcpon)
			break;

		sprintf(var, "mdhcpstart%d", i);
		mdhcpstart = websGetVar(wp, var, NULL);
		if (!mdhcpstart)
			break;

		sprintf(var, "mdhcpmax%d", i);
		mdhcpmax = websGetVar(wp, var, NULL);
		if (!mdhcpmax)
			break;

		sprintf(var, "mdhcpleasetime%d", i);
		mdhcpleasetime = websGetVar(wp, var, NULL);
		if (!mdhcpleasetime)
			break;

		strcat(buffer, mdhcpinterface);
		strcat(buffer, ">");
		strcat(buffer, mdhcpon);
		strcat(buffer, ">");
		strcat(buffer, mdhcpstart);
		strcat(buffer, ">");
		strcat(buffer, mdhcpmax);
		strcat(buffer, ">");
		strcat(buffer, mdhcpleasetime);
		if (i < mdhcpd_count - 1)
			strcat(buffer, " ");
	}
	nvram_set("mdhcpd", buffer);
#endif
#ifdef HAVE_PORTSETUP
	validate_portsetup(wp, NULL, NULL);
#endif

	applytake(value);
}

void add_vlan(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("vlan_tagcount");

	if (realcount == 0) {
		wordlist = nvram_safe_get("vlan_tags");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("vlan_tagcount", var);
	nvram_commit();
	return;
}

void del_vlan(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("vlan_tags");
	newwordlist = (char *)calloc(strlen(wordlist) + 2, 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		} else {
			char *port = word;
			char *tag = strsep(&port, ">");

			if (!tag || !port)
				break;
			char names[32];

			sprintf(names, "%s.%s", tag, port);
			eval("ifconfig", names, "down");
			eval("vconfig", "rem", names);
		}
		count++;
	}

	char var[32];

	realcount = nvram_geti("vlan_tagcount") - 1;
	sprintf(var, "%d", realcount);
	nvram_set("vlan_tagcount", var);
	nvram_set("vlan_tags", newwordlist);
	nvram_commit();
	free(newwordlist);

	return;
}

void add_mdhcp(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("mdhcpd_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("mdhcpd");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("mdhcpd_count", var);
	nvram_commit();
	return;
}

void del_mdhcp(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("mdhcpd");
	newwordlist = (char *)calloc(strlen(wordlist) + 2, 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		}
		count++;
	}

	char var[32];

	realcount = nvram_geti("mdhcpd_count") - 1;
	sprintf(var, "%d", realcount);
	nvram_set("mdhcpd_count", var);
	nvram_set("mdhcpd", newwordlist);
	nvram_commit();
	free(newwordlist);

	return;
}

void del_bridge(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("bridges");
	newwordlist = (char *)calloc(strlen(wordlist) + 2, 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		} else {
			char *port = word;
			char *tag = strsep(&port, ">");
			char *prio = port;

			strsep(&prio, ">");
			if (!tag || !port)
				continue;
			eval("ifconfig", tag, "down");
			eval("brctl", "delbr", tag);
		}
		count++;
	}

	realcount = nvram_geti("bridges_count") - 1;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("bridges_count", var);
	nvram_set("bridges", newwordlist);
	nvram_commit();
	free(newwordlist);

	return;
}

void add_bridge(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("bridges_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("bridges");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("bridges_count", var);
	nvram_commit();
	return;
}

void del_bridgeif(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("bridgesif");
	newwordlist = (char *)calloc(strlen(wordlist) + 2, 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		}
		count++;
	}

	char var[32];

	realcount = nvram_geti("bridgesif_count") - 1;
	sprintf(var, "%d", realcount);
	nvram_set("bridgesif_count", var);
	nvram_set("bridgesif", newwordlist);
	nvram_commit();
	free(newwordlist);

	return;
}

void add_bridgeif(webs_t wp)
{

	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("bridgesif_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("bridgesif");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("bridgesif_count", var);
	nvram_commit();
	return;
}

#endif
#ifdef HAVE_IPVS
void add_ipvs(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("ipvs_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("ipvs");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("ipvs_count", var);
	nvram_commit();
	return;
}

void del_ipvs(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("ipvs");
	if (!strlen(wordlist))
		newwordlist = NULL;
	else
		newwordlist = (char *)calloc(strlen(wordlist), 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		}
		count++;
	}

	realcount = nvram_geti("ipvs_count") - 1;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("ipvs_count", var);
	nvram_set("ipvs", newwordlist);
	nvram_commit();
	if (newwordlist)
		free(newwordlist);
	return;
}

void add_ipvstarget(webs_t wp)
{
	char word[256];
	char *next, *wordlist;
	int count = 0;
	int realcount = nvram_geti("ipvstarget_count");

	if (realcount == 0) {
		wordlist = nvram_safe_get("ipvstarget");
		foreach(word, wordlist, next) {
			count++;
		}
		realcount = count;
	}
	realcount++;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("ipvstarget_count", var);
	nvram_commit();
	return;
}

void del_ipvstarget(webs_t wp)
{
	char word[256];
	int realcount = 0;
	char *next, *wordlist, *newwordlist;
	int todel = websGetVari(wp, "del_value", -1);

	wordlist = nvram_safe_get("ipvstarget");
	if (!strlen(wordlist))
		newwordlist = NULL;
	else
		newwordlist = (char *)calloc(strlen(wordlist), 1);
	int count = 0;

	foreach(word, wordlist, next) {
		if (count != todel) {
			strcat(newwordlist, word);
			strcat(newwordlist, " ");
		}
		count++;
	}

	realcount = nvram_geti("ipvstarget_count") - 1;
	char var[32];

	sprintf(var, "%d", realcount);
	nvram_set("ipvstarget_count", var);
	nvram_set("ipvstarget", newwordlist);
	nvram_commit();
	if (newwordlist)
		free(newwordlist);
	return;
}

#endif

static void save_prefix(webs_t wp, char *prefix)
{
	char n[80];
#ifdef HAVE_MADWIFI
	char turbo[80];
	char chanbw[80];
	int cbwchanged = 0;
#endif
#ifdef HAVE_RELAYD
	char gwaddr[32];
	copytonv(wp, "%s_relayd_gw_auto", prefix);
	copymergetonv(wp, "%s_relayd_gw_ipaddr", prefix);
#endif
#ifdef HAVE_IFL
#ifdef HAVE_NEXTMEDIA
	copytonv(wp, "%s_label", prefix);
#endif
	copytonv(wp, "%s_note", prefix);
#endif
#ifdef HAVE_MADWIFI
	copytonv(wp, "rate_control");
#endif
	snprintf(n, sizeof(n), "%s_ssid", prefix);
	copytonv(wp, n);
	if (!strcmp(prefix, "wl0") || !strcmp(prefix, "wl1") || !strcmp(prefix, "wl2")) {
		char *wl = websGetVar(wp, n, NULL);

		cprintf("copy value %s which is [%s] to nvram\n", n, wl);
		if (wl) {
			if (!strcmp(prefix, "wl0"))
				nvram_set("wl_ssid", wl);
			else if (!strcmp(prefix, "wl1"))
				nvram_set("wl1_ssid", wl);
			else
				nvram_set("wl2_ssid", wl);
		}
	}
	copytonv(wp, "%s_distance", prefix);
#ifdef HAVE_MADWIFI
	{
		snprintf(n, sizeof(n), "%s_txpwrdbm", prefix);
		char *sl = websGetVar(wp, n, NULL);

		if (sl) {
			int base = atoi(sl);
#ifdef HAVE_WIKINGS
/*			if (base > 28)
				base = 28;
#ifdef HAVE_SUB3
			if (base > 25)
				base = 25;
#endif
#ifdef HAVE_SUB6
			if (base > 22)
				base = 22;
#endif*/
#endif

#ifdef HAVE_ESPOD
			if (base > 30)
				base = 30;
#ifdef HAVE_SUB6
			if (base > 30)
				base = 30;
#endif
#ifdef HAVE_SUB3
			if (base > 28)
				base = 28;
#endif
#endif
			int txpower = base - wifi_gettxpoweroffset(prefix);

			if (txpower < 1)
				txpower = 1;
			snprintf(turbo, sizeof(turbo), "%d", txpower);
			nvram_set(n, turbo);
		}
	}
	copytonv(wp, "%s_antgain", prefix);
	copytonv(wp, "%s_regulatory", prefix);
	snprintf(n, sizeof(n), "%s_scanlist", prefix);
	{
		char *sl = websGetVar(wp, n, NULL);

		if (sl) {
			char *slc = strdup(sl);
			int i, sllen = strlen(slc);

			for (i = 0; i < sllen; i++) {
				if (slc[i] == ';')
					slc[i] = ' ';
				if (slc[i] == ',')
					slc[i] = ' ';
			}
			nvram_set(n, slc);
			free(slc);
		}
	}
#ifdef HAVE_MAKSAT
	copytonv(wp, "ath_specialmode");
#endif
	copytonv(wp, "%s_regdomain", prefix);

	copytonv(wp, "%s_rts", prefix);
	if (nvram_nmatch("1", "%s_rts", prefix)) {
		snprintf(turbo, sizeof(turbo), "%s_rtsvalue", prefix);
		char *tw = websGetVar(wp, turbo, NULL);

		if (tw) {
			if (atoi(tw) < 1)
				tw = "1";
			if (atoi(tw) > 2346)
				tw = "2346";
			nvram_nset(tw, "%s_rtsvalue", prefix);
		}
	}
	copytonv(wp, "%s_protmode", prefix);
	copytonv(wp, "%s_minrate", prefix);
	copytonv(wp, "%s_maxrate", prefix);
	copytonv(wp, "%s_xr", prefix);
	copytonv(wp, "%s_outdoor", prefix);
//    copytonv( wp, "%s_compression", prefix ); // Atheros SuperG header
	// compression
	copytonv(wp, "%s_ff", prefix);	// ff = 0, Atheros SuperG fast
	// framing disabled, 1 fast framing
	// enabled
	copytonv(wp, "%s_diversity", prefix);
	copytonv(wp, "%s_preamble", prefix);
#ifdef HAVE_ATH9K
	copytonv(wp, "%s_shortgi", prefix);
	copytonv(wp, "%s_connect", prefix);
	copytonv(wp, "%s_stay", prefix);
	copytonv(wp, "%s_poll_time", prefix);
	copytonv(wp, "%s_strikes", prefix);
#endif
#ifdef HAVE_ATH10K
	copytonv(wp, "%s_subf", prefix);
	copytonv(wp, "%s_mubf", prefix);
#endif
	copytonv(wp, "%s_wmm", prefix);
	copytonv(wp, "%s_bcn", prefix);
	copytonv(wp, "%s_dtim", prefix);
	copytonv(wp, "%s_txantenna", prefix);
	copytonv(wp, "%s_rxantenna", prefix);
	copytonv(wp, "%s_intmit", prefix);
	copytonv(wp, "%s_csma", prefix);
	copytonv(wp, "%s_noise_immunity", prefix);
	copytonv(wp, "%s_ofdm_weak_det", prefix);

	copytonv(wp, "%s_chanshift", prefix);
	copytonv(wp, "%s_doth", prefix);
	copytonv(wp, "%s_maxassoc", prefix);

	snprintf(chanbw, sizeof(chanbw), "%s_channelbw", prefix);
	char *cbw = websGetVar(wp, chanbw, NULL);

	if (cbw && !nvram_match(chanbw, cbw)) {
		cbwchanged = 1;
	}
	if (cbw)
		nvram_set(chanbw, cbw);

	copytonv(wp, "%s_xr", prefix);
	copytonv(wp, "%s_mtikie", prefix);
	copytonv(wp, "%s_cardtype", prefix);

#endif
	copytonv(wp, "%s_closed", prefix);
	if (has_ac(prefix) && has_2ghz(prefix)) {
		copytonv(wp, "%s_turbo_qam", prefix);
	}
	copytonv(wp, "%s_atf", prefix);
	copytonv(wp, "%s_fc", prefix);

#ifdef HAVE_80211AC
#ifndef HAVE_NOAC
	copytonv(wp, "%s_wmf_bss_enable", prefix);
	if (has_ac(prefix)) {
		copytonv(wp, "%s_nitro_qam", prefix);
	}
	if (has_beamforming(prefix)) {
		copytonv(wp, "%s_txbf", prefix);
		copytonv(wp, "%s_itxbf", prefix);
	}
#endif
#endif

#ifndef HAVE_MADWIFI
	char *ifname = "wl0";

#ifndef HAVE_RT2880

	if (!strcmp(prefix, "wl0"))
		ifname = get_wl_instance_name(0);
	else if (!strcmp(prefix, "wl1"))
		ifname = get_wl_instance_name(1);
	else if (!strcmp(prefix, "wl2"))
		ifname = get_wl_instance_name(2);
	else
		ifname = prefix;
#else
	ifname = getRADev(prefix);
#endif
	copytonv(wp, "%s_multicast", ifname);
	copytonv(wp, "%s_bridged", ifname);
	copytonv(wp, "%s_nat", ifname);
	copytonv(wp, "%s_isolation", ifname);
	copytonv(wp, "%s_dns_redirect", ifname);

	copymergetonv(wp, "%s_dns_ipaddr", ifname);
	copymergetonv(wp, "%s_ipaddr", ifname);
	copymergetonv(wp, "%s_netmask", ifname);

#else

	copytonv(wp, "%s_multicast", prefix);
	copytonv(wp, "%s_bridged", prefix);
	copytonv(wp, "%s_nat", prefix);
	copytonv(wp, "%s_isolation", prefix);
	copytonv(wp, "%s_dns_redirect", prefix);
	copymergetonv(wp, "%s_dns_ipaddr", prefix);
	copymergetonv(wp, "%s_ipaddr", prefix);
	copymergetonv(wp, "%s_netmask", prefix);

	copytonv(wp, "%s_duallink", prefix);

	copymergetonv(wp, "%s_duallink_parent", prefix);

#endif

	copytonv(wp, "%s_ap_isolate", prefix);
	snprintf(n, sizeof(n), "%s_mode", prefix);
	char *wl_newmode = websGetVar(wp, n, NULL);
	if (wl_newmode && (nvram_match(n, "sta") || nvram_match(n, "apsta")) && strcmp(wl_newmode, "sta") && strcmp(wl_newmode, "apsta"))
		notifywanChange();

	if (wl_newmode && nvram_match(n, "ap") && (!strcmp(wl_newmode, "sta") || !strcmp(wl_newmode, "apsta")))
		notifywanChange();

	if (nvram_match(n, "sta")) {

		if (wl_newmode)
			if ((!strcmp(wl_newmode, "ap") || !strcmp(wl_newmode, "wdsap")
			     || !strcmp(wl_newmode, "infra") || !strcmp(wl_newmode, "wdssta"))
			    && nvram_invmatch("wan_proto", "3g")) {
				nvram_set("wan_proto", "disabled");
			}
	}
	copytonv(wp, n);
	if (!strcmp(prefix, "wl0") || !strcmp(prefix, "wl1") || !strcmp(prefix, "wl2")) {
		char *wl = websGetVar(wp, n, NULL);

		cprintf("copy value %s which is [%s] to nvram\n", n, wl);
		if (wl && !strcmp(prefix, "wl0"))
			nvram_set("wl_mode", wl);
#ifndef HAVE_MADWIFI
		if (wl && strcmp(wl, "ap") && strcmp(wl, "apsta")
		    && strcmp(wl, "apstawet")) {
			nvram_nset("", "%s_vifs", prefix);
		}
#endif
	}
	int chanchanged = 0;

#ifdef HAVE_RT2880
	copytonv(wp, "%s_greenfield", prefix);
#endif

#ifndef HAVE_MADWIFI
	if (!strcmp(prefix, "wl0") || !strcmp(prefix, "wl1") || !strcmp(prefix, "wl2"))
#endif
	{
		snprintf(n, sizeof(n), "%s_net_mode", prefix);
		if (!nvram_match(n, websGetVar(wp, n, ""))) {
			chanchanged = 1;
			copytonv(wp, n);
			char *value = websGetVar(wp, n, "");
			if (!strcmp(prefix, "wl0"))
#ifndef HAVE_MADWIFI
				convert_wl_gmode(value, "wl0");
#else
				convert_wl_gmode(value, "wl");
#endif
			else
				convert_wl_gmode(value, prefix);
		}
	}
#ifdef HAVE_MADWIFI
#if 0
	if (cbwchanged || chanchanged) {
		snprintf(sifs, sizeof(sifs), "%s_sifstime", prefix);
		snprintf(preamble, sizeof(preamble), "%s_preambletime", prefix);
		if (nvram_matchi(chanbw, 40)) {
			nvram_seti(sifs, 8);
			nvram_seti(preamble, 14);
		} else if (nvram_matchi(chanbw, 5)) {
			nvram_seti(sifs, 64);
			nvram_seti(preamble, 80);
		} else if (nvram_matchi(chanbw, 10)) {
			nvram_seti(sifs, 32);
			nvram_seti(preamble, 40);
		} else {
			nvram_seti(sifs, 16);
			nvram_seti(preamble, 20);
		}

	}
#endif
#endif

	copytonv(wp, "%s_nbw", prefix);
	copytonv(wp, "%s_nctrlsb", prefix);

	snprintf(n, sizeof(n), "%s_channel", prefix);
	if (!strcmp(prefix, "wl0") || !strcmp(prefix, "wl1") || !strcmp(prefix, "wl2")) {
		char *wl = websGetVar(wp, n, NULL);

		cprintf("copy value %s which is [%s] to nvram\n", n, wl);
		if (wl && !strcmp(prefix, "wl0"))
			nvram_set("wl_channel", wl);
		else if (wl && !strcmp(prefix, "wl1"))
			nvram_set("wl1_channel", wl);
		else
			nvram_set("wl2_channel", wl);
	}
	copytonv(wp, n);

	snprintf(n, sizeof(n), "%s_wchannel", prefix);
	if (!strcmp(prefix, "wl0") || !strcmp(prefix, "wl1") || !strcmp(prefix, "wl2")) {
		char *wl = websGetVar(wp, n, NULL);

		cprintf("copy value %s which is [%s] to nvram\n", n, wl);
		if (wl && !strcmp(prefix, "wl0"))
			nvram_set("wl_wchannel", wl);
		else if (wl && !strcmp(prefix, "wl1"))
			nvram_set("wl1_wchannel", wl);
		else
			nvram_set("wl2_wchannel", wl);

	}

	copytonv(wp, n);
	copytonv(wp, "wl_reg_mode");
	copytonv(wp, "wl_tpc_db");

#if defined(HAVE_NORTHSTAR) || defined(HAVE_80211AC) && !defined(HAVE_BUFFALO)
	snprintf(n, sizeof(n), "wl_regdomain");
	char *reg = websGetVar(wp, n, NULL);
	if (reg) {
		if (strcmp(nvram_safe_get("wl_regdomain"), reg)) {
			setRegulationDomain(reg);
			eval("restart", "lan");
		}
	}
	copytonv(wp, "wl_regdomain");
#endif
}

void wireless_join(webs_t wp)
{
	char *value = websGetVar(wp, "action", "");
	char *ssid = websGetVar(wp, "wl_ssid", NULL);
	if (ssid) {
		char *wifi = nvram_safe_get("wifi_display");
		if (strlen(wifi) > 0) {
			if (!strcmp(wifi, "ath0"))
				nvram_set("wl_ssid", ssid);
			if (!strcmp(wifi, "wl0"))
				nvram_set("wl_ssid", ssid);
			nvram_nset(ssid, "%s_ssid", wifi);
			nvram_set("cur_ssid", ssid);
			nvram_commit();
		}

	}
	applytake(value);
}

void wireless_save(webs_t wp)
{
	char *value = websGetVar(wp, "action", "");

	char *next;
	char var[80];

#ifndef HAVE_MADWIFI
	int c = get_wl_instances();
	int i;

	for (i = 0; i < c; i++) {
		char buf[16];

		sprintf(buf, "wl%d", i);
		save_prefix(wp, buf);
		char *vifs = nvram_nget("wl%d_vifs", i);
#else
	int c = getdevicecount();
	int i;

	for (i = 0; i < c; i++) {
		char buf[16];

		sprintf(buf, "ath%d", i);
		save_prefix(wp, buf);
		char *vifs = nvram_nget("ath%d_vifs", i);

#endif
		if (vifs == NULL)
			return;
		foreach(var, vifs, next) {
			save_prefix(wp, var);
		}
#ifdef HAVE_ATH9K
		copytonv(wp, "radio%d_timer_enable", i);
		copytonv(wp, "radio%d_on_time", i);
#endif
	}

#ifdef HAVE_ERC
	struct variable filter_variables[] = {
	      {argv:ARGV("1", "0")},
	      {argv:ARGV("1", "0")},
	}, *which;

	char *rd_off, *rd_boot_off;

	rd_off = websGetVar(wp, "radiooff_button", NULL);
	rd_off = websGetVar(wp, "radiooff_button", NULL);
	if (!rd_off && !valid_choice(wp, rd_off, &which[0])) {
		return;
	}
	nvram_set("radiooff_button", rd_off);

	rd_boot_off = websGetVar(wp, "radiooff_boot_off", NULL);
	if (!rd_boot_off && !valid_choice(wp, rd_boot_off, &which[1])) {
		return;
	}
	nvram_set("radiooff_boot_off", rd_boot_off);
#endif
	// nvram_commit ();
	applytake(value);
#ifdef HAVE_GUESTPORT
	eval("stopservice", "firewall");
	eval("startservice", "firewall");
#endif
}

void hotspot_save(webs_t wp)
{
#ifdef HAVE_TIEXTRA1
	chillispot_save(wp);
#endif
#ifdef HAVE_TIEXTRA2
	wifidogs_save(wp);
#endif
	validate_cgi(wp);
}

#ifdef HAVE_WIVIZ
void set_wiviz(webs_t wp)
{

	char *hopdwell = websGetVar(wp, "hopdwell", NULL);
	char *hopseq = websGetVar(wp, "hopseq", NULL);
	FILE *fp = fopen("/tmp/wiviz2-cfg", "wb");

	if (strstr(hopseq, ","))
		fprintf(fp, "channelsel=hop&");
	else
		fprintf(fp, "channelsel=%s&", hopseq);

	fprintf(fp, "hopdwell=%s&hopseq=%s\n", hopdwell, hopseq);

	nvram_set("hopdwell", hopdwell);
	nvram_set("hopseq", hopseq);

	fclose(fp);
	killall("wiviz", SIGUSR2);

}
#endif

void ttraff_erase(webs_t wp)
{
	char line[2048];
	char *name = NULL;

	FILE *fp = popen("nvram show | grep traff-", "r");

	if (fp == NULL) {
		return;
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (startswith(line, "traff-")) {
			name = strtok(line, "=");
			if (strlen(name) == 13)	//only unset ttraf-XX-XXXX
			{
				nvram_unset(name);
			}
		}
	}
	pclose(fp);
	nvram_commit();
}

void changepass(webs_t wp)
{
	char *value = websGetVar(wp, "http_username", NULL);
	char *pass = websGetVar(wp, "http_passwd", NULL);

	if (value && pass && strcmp(value, TMP_PASSWD)
	    && valid_name(wp, value, NULL)) {
		char passout[MD5_OUT_BUFSIZE];
		nvram_set("http_username", zencrypt(value, passout));

		eval("/sbin/setpasswd");
#ifdef HAVE_IAS
		nvram_set("http_userpln", value);
#endif
	}

	if (pass && value && strcmp(pass, TMP_PASSWD)
	    && valid_name(wp, pass, NULL)) {
		char passout[MD5_OUT_BUFSIZE];
		if (nvram_match("http_username", DEFAULT_PASS))
			nvram_seti("unblock", 1);
		nvram_set("http_passwd", zencrypt(pass, passout));

		eval("/sbin/setpasswd");
#ifdef HAVE_IAS
		nvram_set("http_pwdpln", pass);
#endif
	}
	nvram_commit();
}

#ifdef HAVE_CHILLILOCAL

void user_remove(webs_t wp)
{
	macro_rem("fon_usernames", "fon_userlist");
}

void user_add(webs_t wp)
{
	macro_add("fon_usernames");
	// validate_userlist(wp);
}
#endif

#ifdef HAVE_RADLOCAL

void raduser_add(webs_t wp)
{
	int radcount = 0;
	char *radc = nvram_get("iradius_count");

	if (radc != NULL)
		radcount = atoi(radc);
	radcount++;
	char count[16];

	sprintf(count, "%d", radcount);
	nvram_set("iradius_count", count);
}
#endif

#ifdef HAVE_MILKFISH
void milkfish_sip_message(webs_t wp)
{
	char *message = websGetVar(wp, "sip_message", NULL);
	char *dest = websGetVar(wp, "sip_message_dest", NULL);
	int i;
	FILE *fp = fopen("/tmp/sipmessage", "wb");

	if (fp == NULL)
		return;
	char *host_key = message;

	i = 0;
	do {
		if (host_key[i] != 0x0D)
			fprintf(fp, "%c", host_key[i]);
	}
	while (host_key[++i]);
	putc(0xa, fp);
	fclose(fp);
	eval("milkfish_services", "simpledd", dest);
	return;
}
#endif

void set_security(webs_t wp)
{
	char *prefix = websGetVar(wp, "security_varname", "security_mode");

	char *ifname = websGetVar(wp, "ifname", NULL);
	char *prefix2 = websGetVar(wp, prefix, "disabled");

	nvram_set(prefix, prefix2);
#ifdef HAVE_MADWIFI
	char n2[32];
	char akm[128] = { 0, 0 };
	if (ifname && !strcmp(prefix2, "wpa")) {
		sprintf(n2, "%s_akm", ifname);
		_copytonv(wp, "%s_psk", ifname);
		_copytonv(wp, "%s_psk2", ifname);
		_copytonv(wp, "%s_psk2-sha256", ifname);
		_copytonv(wp, "%s_psk3", ifname);
		if (nvram_nmatch("1", "%s_psk", ifname))
			sprintf(akm, "%s %s", akm, "psk");
		if (nvram_nmatch("1", "%s_psk2", ifname))
			sprintf(akm, "%s %s", akm, "psk2");
		if (nvram_nmatch("1", "%s_psk2-256", ifname))
			sprintf(akm, "%s %s", akm, "psk2-256");
		if (nvram_nmatch("1", "%s_psk3", ifname))
			sprintf(akm, "%s %s", akm, "psk3");
		nvram_set(n2, &akm[1]);
	}
	if (ifname && !strcmp(prefix2, "8021X")) {
		sprintf(n2, "%s_akm", ifname);
		_copytonv(wp, "%s_802.1x", ifname);
		_copytonv(wp, "%s_leap", ifname);
		_copytonv(wp, "%s_peap", ifname);
		_copytonv(wp, "%s_tls", ifname);
		_copytonv(wp, "%s_ttls", ifname);
		_copytonv(wp, "%s_wpa", ifname);
		_copytonv(wp, "%s_wpa2", ifname);
		_copytonv(wp, "%s_wpa2-sha256", ifname);
		_copytonv(wp, "%s_wpa3", ifname);
		_copytonv(wp, "%s_wpa3-192", ifname);
		_copytonv(wp, "%s_wpa3-128", ifname);
		if (nvram_nmatch("1", "%s_leap", ifname))
			sprintf(akm, "%s %s", akm, "leap");
		if (nvram_nmatch("1", "%s_peap", ifname))
			sprintf(akm, "%s %s", akm, "peap");
		if (nvram_nmatch("1", "%s_tls", ifname))
			sprintf(akm, "%s %s", akm, "tls");
		if (nvram_nmatch("1", "%s_ttls", ifname))
			sprintf(akm, "%s %s", akm, "ttls");
		nvram_set(n2, &akm[1]);
	}
	if (ifname && (!strcmp(prefix2, "wpa") || !strcmp(prefix2, "8021X"))) {
		sprintf(n2, "%s_akm", ifname);
		_copytonv(wp, "%s_ccmp", ifname);
		_copytonv(wp, "%s_tkip", ifname);
		_copytonv(wp, "%s_ccmp-256", ifname);
		_copytonv(wp, "%s_gcmp-256", ifname);
		_copytonv(wp, "%s_gcmp", ifname);
		_copytonv(wp, "%s_wpa", ifname);
		_copytonv(wp, "%s_wpa2", ifname);
		_copytonv(wp, "%s_wpa2-sha256", ifname);
		_copytonv(wp, "%s_wpa3", ifname);
		_copytonv(wp, "%s_wpa3-192", ifname);
		_copytonv(wp, "%s_wpa3-128", ifname);
		if (nvram_nmatch("1", "%s_wpa", ifname))
			sprintf(akm, "%s %s", akm, "wpa");
		if (nvram_nmatch("1", "%s_wpa2", ifname))
			sprintf(akm, "%s %s", akm, "wpa2");
		if (nvram_nmatch("1", "%s_wpa2-sha256", ifname))
			sprintf(akm, "%s %s", akm, "wpa2-sha256");
		if (nvram_nmatch("1", "%s_wpa3", ifname))
			sprintf(akm, "%s %s", akm, "wpa3");
		if (nvram_nmatch("1", "%s_wpa3-192", ifname))
			sprintf(akm, "%s %s", akm, "wpa3-192");
		if (nvram_nmatch("1", "%s_wpa3-128", ifname))
			sprintf(akm, "%s %s", akm, "wpa3-128");
		nvram_set(n2, &akm[1]);
	}
#endif

}

void base64_encode(const unsigned char *in, size_t inlen, unsigned char *out, size_t outlen)
{
	static const char b64str[64] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

	while (inlen && outlen) {
		*out++ = b64str[(in[0] >> 2) & 0x3f];
		if (!--outlen)
			break;
		*out++ = b64str[((in[0] << 4) + (--inlen ? in[1] >> 4 : 0)) & 0x3f];
		if (!--outlen)
			break;
		*out++ = (inlen ? b64str[((in[1] << 2) + (--inlen ? in[2] >> 6 : 0)) & 0x3f] : '=');
		if (!--outlen)
			break;
		*out++ = inlen ? b64str[in[2] & 0x3f] : '=';
		if (!--outlen)
			break;
		if (inlen)
			inlen--;
		if (inlen)
			in += 3;
	}

	if (outlen)
		*out = '\0';
}

char *request_freedns(char *user, char *password)
{
	unsigned char final[32];
	char un[128];

	unlink("/tmp/.hash");
	sprintf(un, "%s|%s", user, password);
	sha1_ctx_t context;

	sha1_begin(&context);
	sha1_hash(un, strlen(un), &context);
	sha1_end(final, &context);
	char request[128] = {
		0
	};
	int i;

	for (i = 0; i < 20; i++)
		sprintf(request, "%s%02x", request, final[i]);
	unlink("/tmp/.hash");
	char url[128];
	snprintf(url, sizeof(url), "http://freedns.afraid.org/api/?action=getdyndns&sha=%s", request);
	eval("wget", url, "-O", "/tmp/.hash");
	FILE *in = fopen("/tmp/.hash", "rb");

	if (in == NULL)
		return NULL;
	while (getc(in) != '?' && feof(in) == 0) ;
	i = 0;
	char *hash = safe_malloc(64);

	if (feof(in)) {
		free(hash);
		fclose(in);
		return NULL;
	}
	for (i = 0; i < 63 && feof(in) == 0; i++) {
		hash[i] = getc(in);
		if (hash[i] == EOF)
			break;
	}
	fclose(in);
	hash[i] = 0;
	return hash;
}

static void getddns_userdata(int enable, char *_username, char *_passwd, char *_hostname)
{

	switch (enable) {
	case 1:
		// dyndns
		sprintf(_username, "ddns_username");
		sprintf(_passwd, "ddns_passwd");
		sprintf(_hostname, "ddns_hostname");
		break;
	case 2:
	case 3:		// zoneedit
	case 4:		// no-ip
	case 5:
	case 6:
	case 7:
	case 8:		// tzo
	case 9:		// dynSIP
	case 10:		// dtdns
	case 11:		//duiadns
		// 3322 dynamic : added botho 30/07/06
		// easydns
		sprintf(_username, "ddns_username_%d", enable);
		sprintf(_passwd, "ddns_passwd_%d", enable);
		sprintf(_hostname, "ddns_hostname_%d", enable);

		break;
	}

}

void ddns_save_value(webs_t wp)
{
	char *username, *passwd, *hostname, *dyndnstype, *wildcard, *custom, *conf, *url, *wan_ip;
	int force;
	char _username[] = "ddns_username_XX";
	char _passwd[] = "ddns_passwd_XX";
	char _hostname[] = "ddns_hostname_XX";
	char _dyndnstype[] = "ddns_dyndnstype_XX";
	char _wildcard[] = "ddns_wildcard_XX";
	char _custom[] = "ddns_custom_XX";
	char _conf[] = "ddns_conf";
	char _url[] = "ddns_url";
	char _force[] = "ddns_force";
	char _wan_ip[] = "ddns_wan_ip";

	int enable = websGetVari(wp, "ddns_enable", -1);
	if (enable > 11 || enable < 0) {
		return;
	}
	int gethash = 0;

	int i;
	for (i = 1; i < 12; i++) {
		if (i == enable)
			continue;
		getddns_userdata(i, _username, _passwd, _hostname);
		nvram_unset(_username);
		nvram_unset(_passwd);
		nvram_unset(_hostname);
	}
	getddns_userdata(enable, _username, _passwd, _hostname);

	switch (enable) {
	case 0:
		// Disable
		nvram_seti("ddns_enable", enable);
		return;
		break;
	case 1:
		// dyndns

		snprintf(_dyndnstype, sizeof(_dyndnstype), "ddns_dyndnstype");
		snprintf(_wildcard, sizeof(_wildcard), "ddns_wildcard");
		break;
	case 2:
		// afraid
		gethash = 1;
		break;
	case 5:
		// custom
		snprintf(_custom, sizeof(_custom), "ddns_custom_%d", enable);
		snprintf(_conf, sizeof(_conf), "ddns_conf");
		snprintf(_url, sizeof(_url), "ddns_url");
		break;
	case 6:
		// 3322 dynamic : added botho 30/07/06
		snprintf(_dyndnstype, sizeof(_dyndnstype), "ddns_dyndnstype_%d", enable);
		snprintf(_wildcard, sizeof(_wildcard), "ddns_wildcard_%d", enable);
		break;
	case 7:
		// easydns
		snprintf(_wildcard, sizeof(_wildcard), "ddns_wildcard_%d", enable);
		break;
	}

	username = websGetVar(wp, _username, NULL);
	passwd = websGetVar(wp, _passwd, NULL);
	hostname = websGetVar(wp, _hostname, NULL);
	dyndnstype = websGetVar(wp, _dyndnstype, NULL);
	wildcard = websGetVar(wp, _wildcard, NULL);
	custom = websGetVar(wp, _custom, NULL);
	conf = websGetVar(wp, _conf, NULL);
	url = websGetVar(wp, _url, NULL);
	force = websGetVari(wp, _force, 0);
	wan_ip = websGetVar(wp, _wan_ip, NULL);

	if (!username || !passwd || !hostname || !force || !wan_ip) {
		return;
	}

	if (force < 1 || force > 60) {
		force = 10;
	}

	nvram_seti("ddns_enable", enable);
	nvram_set(_username, username);
	if (strcmp(passwd, TMP_PASSWD)) {
		nvram_set(_passwd, passwd);
	}
	if (gethash && !strchr(hostname, ',')) {
		char hostn[128];
		char *hash = request_freedns(username, nvram_safe_get(_passwd));

		if (hash) {
			sprintf(hostn, "%s,%s", hostname, hash);
			nvram_set(_hostname, hostn);
			free(hash);
		} else {
			nvram_set(_hostname, "User/Password wrong");
		}
	} else
		nvram_set(_hostname, hostname);

	nvram_set(_dyndnstype, dyndnstype);
	nvram_set(_wildcard, wildcard);
	nvram_set(_custom, custom);
	nvram_set(_conf, conf);
	nvram_set(_url, url);
	nvram_seti(_force, force);
	nvram_set(_wan_ip, wan_ip);

}

void ddns_update_value(webs_t wp)
{

}

void port_vlan_table_save(webs_t wp)
{
	int port = 0, vlan = 0, vlans[22], i;
	char portid[32], portvlan[64], buff[32] = { 0 }, *c, *next, br0vlans[64], br1vlans[64], br2vlans[64];
	int portval;
	strcpy(portvlan, "");

	for (vlan = 0; vlan < 22; vlan++)
		vlans[vlan] = 0;

	vlans[16] = 1;

	for (port = 0; port < 5; port++) {
		for (vlan = 0; vlan < 22; vlan++) {
			snprintf(portid, sizeof(portid), "port%dvlan%d", port, vlan);
			char *s_portval = websGetVar(wp, portid, "");

			if (vlan < 17 || vlan > 21)
				i = (strcmp(s_portval, "on") == 0);
			else
				i = (strcmp(s_portval, "on") != 0);

			if (i) {
				if (strlen(portvlan) > 0)
					strcat(portvlan, " ");

				snprintf(buff, 4, "%d", vlan);
				strcat(portvlan, buff);
				vlans[vlan] = 1;
			}
		}

		snprintf(portid, sizeof(portid), "port%dvlans", port);
		nvram_set(portid, portvlan);
		strcpy(portvlan, "");
	}

	/*
	 * done with ports 0-4, now set up #5 automaticly 
	 */
	/*
	 * if a VLAN is used, it also gets assigned to port #5 
	 */
	for (vlan = 0; vlan < 17; vlan++) {
		if (vlans[vlan]) {
			if (strlen(portvlan) > 0)
				strcat(portvlan, " ");

			snprintf(buff, 4, "%d", vlan);
			strcat(portvlan, buff);
		}
	}

	nvram_set("port5vlans", portvlan);

	strcpy(br0vlans, "");
	c = nvram_safe_get("lan_ifnames");
	if (c) {
		foreach(portid, c, next) {
			if (!(strncmp(portid, "vlan", 4) == 0)
			    && !(strncmp(portid, "eth1", 4) == 0)) {
				if (strlen(br0vlans) > 0)
					strcat(br0vlans, " ");
				strcat(br0vlans, portid);
			}
		}
	}

	strcpy(br1vlans, "");
	c = nvram_safe_get("ub1_ifnames");
	if (c) {
		foreach(portid, c, next) {
			if (!(strncmp(portid, "vlan", 4) == 0)
			    && !(strncmp(portid, "eth1", 4) == 0)) {
				if (strlen(br1vlans) > 0)
					strcat(br1vlans, " ");
				strcat(br1vlans, portid);
			}
		}
	}

	strcpy(br2vlans, "");
	c = nvram_safe_get("ub2_ifnames");
	if (c) {
		foreach(portid, c, next) {
			if (!(strncmp(portid, "vlan", 4) == 0)
			    && !(strncmp(portid, "eth1", 4) == 0)) {
				if (strlen(br2vlans) > 0)
					strcat(br2vlans, " ");
				strcat(br2vlans, portid);
			}
		}
	}

	for (i = 0; i < 16; i++) {
		snprintf(buff, sizeof(buff), "vlan%d", i);
		portval = websGetVari(wp, buff, -1);

		switch (portval) {
		case 0:
			if (strlen(br0vlans) > 0)
				strcat(br0vlans, " ");
			strcat(br0vlans, buff);
			break;
		case 1:
			if (strlen(br1vlans) > 0)
				strcat(br1vlans, " ");
			strcat(br1vlans, buff);
			break;
		case 2:
			if (strlen(br2vlans) > 0)
				strcat(br2vlans, " ");
			strcat(br2vlans, buff);
			break;
		}
	}

	strcpy(buff, "");

	switch (websGetVari(wp, "wireless", -1)) {
	case 0:
		if (strlen(br0vlans) > 0)
			strcat(br0vlans, " ");
		strcat(br0vlans, get_wdev());
		break;
	case 1:
		if (strlen(br1vlans) > 0)
			strcat(br1vlans, " ");
		strcat(br1vlans, get_wdev());
		break;
	case 2:
		if (strlen(br2vlans) > 0)
			strcat(br2vlans, " ");
		strcat(br2vlans, get_wdev());
		break;
	}

	snprintf(buff, 3, "%s", websGetVar(wp, "trunking", ""));

	nvram_set("lan_ifnames", br0vlans);
	// nvram_set("ub1_ifnames", br1vlans);
	// nvram_set("ub2_ifnames", br2vlans);
	nvram_set("trunking", buff);
	nvram_seti("vlans", 1);

	nvram_commit();

}

static void save_macmode_if(webs_t wp, char *ifname)
{

	char macmode[32];
	char macmode1[32];

	sprintf(macmode, "%s_macmode", ifname);
	sprintf(macmode1, "%s_macmode1", ifname);
	rep(macmode1, '.', 'X');
	char *wl_macmode1, *wl_macmode;

	wl_macmode = websGetVar(wp, macmode, NULL);
	wl_macmode1 = websGetVar(wp, macmode1, NULL);

	if (!wl_macmode1)
		return;

	if (!strcmp(wl_macmode1, "disabled")) {
		nvram_set(macmode1, "disabled");
		nvram_set(macmode, "disabled");
	} else if (!strcmp(wl_macmode1, "other")) {
		if (!wl_macmode)
			nvram_set(macmode, "deny");
		else
			nvram_set(macmode, wl_macmode);
		nvram_set(macmode1, "other");
	}
}

void save_macmode(webs_t wp)
{
#ifndef HAVE_MADWIFI
	int c = get_wl_instances();
	char devs[32];
	int i;

	for (i = 0; i < c; i++) {
		sprintf(devs, "wl%d", i);
		save_macmode_if(wp, devs);
	}
#else
	int c = getdevicecount();
	char devs[32];
	int i;

	for (i = 0; i < c; i++) {
		sprintf(devs, "ath%d", i);
		save_macmode_if(wp, devs);
		char vif[32];

		sprintf(vif, "%s_vifs", devs);
		char var[80], *next;
		char *vifs = nvram_safe_get(vif);

		if (vifs != NULL)
			foreach(var, vifs, next) {
			save_macmode_if(wp, var);
			}
	}

#endif
	return;

}

// handle UPnP.asp requests / added 10
void tf_upnp(webs_t wp)
{
	char *v;
	char s[64];

	if (((v = websGetVar(wp, "remove", NULL)) != NULL) && (*v)) {
		if (strcmp(v, "all") == 0) {
			nvram_seti("upnp_clear", 1);
		} else {
			int which = nvram_default_geti("forward_cur", 0);
			int i = atoi(v);
			char val[32];

			sprintf(val, "forward_port%d", i);
			int a;

			nvram_unset(val);
			for (a = i + 1; a < which; a++) {
				nvram_nset(nvram_nget("forward_port%d", a), "forward_port%d", a - 1);
			}
			which--;
			sprintf(val, "forward_port%d", which);
			nvram_unset(val);
			if (which < 0)
				which = 0;
			sprintf(val, "%d", which);
			nvram_set("forward_cur", val);
		}
		eval("stopservice", "firewall");
		eval("startservice", "firewall");	//restart firewall
	}

}

#ifdef HAVE_MINIDLNA
#include <dlna.h>
static void dlna_save(webs_t wp)
{
	int c, j;
	char var[128], val[128];
	json_t *entry = NULL;

	// dlna shares
	json_t *entries = json_array();
	int share_number = websGetVari(wp, "dlna_shares_count", 0);
	for (c = 1; c <= share_number; c++) {
		entry = json_object();
		sprintf(var, "dlnashare_mp_%d", c);
		json_object_set_new(entry, "mp", json_string(websGetVar(wp, var, "")));
		sprintf(var, "dlnashare_subdir_%d", c);
		json_object_set_new(entry, "sd", json_string(websGetVar(wp, var, "")));
		int type = 0;
		sprintf(var, "dlnashare_audio_%d", c);
		if (websGetVari(wp, var, 0))
			type |= TYPE_AUDIO;
		sprintf(var, "dlnashare_video_%d", c);
		if (websGetVari(wp, var, 0))
			type |= TYPE_VIDEO;
		sprintf(var, "dlnashare_images_%d", c);
		if (websGetVari(wp, var, 0))
			type |= TYPE_IMAGES;
		json_object_set_new(entry, "types", json_integer(type));
		json_array_append(entries, entry);
	}
	nvram_set("dlna_shares", json_dumps(entries, JSON_COMPACT));
	json_array_clear(entries);
}
#endif

#ifdef HAVE_NAS_SERVER
#include <samba3.h>
void nassrv_save(webs_t wp)
{
#ifdef HAVE_SAMBA_SERVER
	int c, j;
	char var[128], val[128];
	json_t *entry = NULL, *user_entries;

	// samba shares
	json_t *entries = json_array();
	int share_number = websGetVari(wp, "samba_shares_count", 0);
	int user_number = websGetVari(wp, "samba_users_count", 0);
	for (c = 1; c <= share_number; c++) {
		entry = json_object();
		sprintf(var, "smbshare_mp_%d", c);
		json_object_set_new(entry, "mp", json_string(websGetVar(wp, var, "")));
		sprintf(var, "smbshare_subdir_%d", c);
		json_object_set_new(entry, "sd", json_string(websGetVar(wp, var, "")));
		sprintf(var, "smbshare_label_%d", c);
		json_object_set_new(entry, "label", json_string(websGetVar(wp, var, "")));
		sprintf(var, "smbshare_public_%d", c);
		json_object_set_new(entry, "public", json_integer(websGetVari(wp, var, 0)));
		sprintf(var, "smbshare_access_perms_%d", c);
		sprintf(val, "%s", websGetVar(wp, var, "-"));
		if (!strcmp(val, "-")) {
			sprintf(var, "smbshare_access_perms_prev_%d", c);
			sprintf(val, "%s", websGetVar(wp, var, "x"));
		}
		json_object_set_new(entry, "perms", json_string(val));
		user_entries = json_array();
		for (j = 1; j <= user_number; j++) {
			sprintf(var, "smbshare_%d_user_%d", c, j);
			if (!strcmp(websGetVar(wp, var, ""), "1")) {
				sprintf(var, "smbuser_username_%d", j);
				json_array_append(user_entries, json_string(websGetVar(wp, var, "")));
			}
		}
		json_object_set_new(entry, "users", user_entries);
		json_array_append(entries, entry);
	}
	//fprintf(stderr, "[SAVE NAS] %s\n", json_dumps( entries, JSON_COMPACT ) );
	nvram_set("samba3_shares", json_dumps(entries, JSON_COMPACT));
	json_array_clear(entries);

	entries = json_array();
	for (c = 1; c <= user_number; c++) {
		entry = json_object();
		sprintf(var, "smbuser_username_%d", c);
		json_object_set_new(entry, "user", json_string(websGetVar(wp, var, "")));
		sprintf(var, "smbuser_password_%d", c);
		json_object_set_new(entry, "pass", json_string(websGetVar(wp, var, "")));
		int type = 0;
		sprintf(var, "smbuser_samba_%d", c);
		if (websGetVari(wp, var, 0))
			type |= SHARETYPE_SAMBA;
		sprintf(var, "smbuser_ftp_%d", c);
		if (websGetVari(wp, var, 0))
			type |= SHARETYPE_FTP;
		json_object_set_new(entry, "type", json_integer(type));
		json_array_append(entries, entry);
	}
	//fprintf(stderr, "[SAVE NAS USERS] %s\n", json_dumps( entries, JSON_COMPACT ) );
	nvram_set("samba3_users", json_dumps(entries, JSON_COMPACT));
	json_array_clear(entries);
#endif
	char *value = websGetVar(wp, "action", "");

	// all other vars
	validate_cgi(wp);

	addAction("nassrv");
	nvram_seti("nowebaction", 1);
#ifdef HAVE_MINIDLNA
	dlna_save(wp);
#endif
#ifdef HAVE_RAID
	raid_save(wp);
#endif

	applytake(value);
}
#endif

#ifdef HAVE_SPOTPASS
void nintendo_save(webs_t wp)
{

	char prefix[16] = "ath0";
	char var[32], param[32];
	int device = 0;

	int enabled = nvram_default_geti("spotpass", 0);

	device = prefix[strlen(prefix) - 1] - '0';

	// handle server list
	int count = 0;
	char *buffer = (char *)safe_malloc(strlen(websGetVar(wp, "spotpass_servers", "")) + 1);
	strcpy(buffer, websGetVar(wp, "spotpass_servers", ""));

	char *ptr = strtok(buffer, "\n");
	while (ptr != NULL) {
		count++;
		ptr = strtok(NULL, "\n");
	}
	char *serverlist = (char *)safe_malloc(strlen(websGetVar(wp, "spotpass_servers", "")) + (count * 2) + 1);
	char line[256], url[128], proto[8], mode[16], ports[64];
	int port1, port2, lines = 0;

	strcpy(buffer, websGetVar(wp, "spotpass_servers", ""));
	strcpy(serverlist, "\0");
	fprintf(stderr, "%s\n", buffer);
	ptr = strtok(buffer, "\n");
	while (ptr != NULL) {
		strcpy(line, "\0");
		if (sscanf(ptr, "%s %s %s %d %d", &url, &proto, &mode, &port1, &port2) == 5) {
			sprintf(line, "%s %s %d,%d", url, proto, port1, port2);
		} else if (sscanf(ptr, "%s %s %d %d", &url, &proto, &port1, &port2) == 4) {
			sprintf(line, "%s %s %d,%d", url, proto, port1, port2);
		} else if (sscanf(ptr, "%s %s %s", &url, &proto, &ports) == 3) {
			sprintf(line, "%s %s %s", url, proto, ports);
		}
		lines++;
		if (strlen(line) > 0) {
			strcat(serverlist, line);
			if (lines < count) {
				strcat(serverlist, "|");
			}
		}
		ptr = strtok(NULL, "\n");
	}
	nvram_set("spotpass_servers", serverlist);

	if (enabled == 0 && !strcmp(websGetVar(wp, "spotpass", "0"), "1")) {

		// check if vap is set
		if (!strcmp(nvram_default_get("spotpass_vif", ""), "")) {

			int count = get_vifcount(prefix) + 1;
			add_vifs_single(prefix, device);
			sprintf(var, "%s.%d", prefix, count);
			nvram_set("spotpass_vif", var);

			// set parameters for vap
			sprintf(param, "%s_ssid", var);
			nvram_set(param, "NintendoSpotPass1");
			sprintf(param, "%s_bridged", var);
			nvram_seti(param, 0);
			sprintf(param, "%s_ipaddr", var);
			nvram_set(param, "192.168.12.1");
			sprintf(param, "%s_netmask", var);
			nvram_set(param, "255.255.255.0");
			sprintf(param, "%s_macmode", var);
			nvram_set(param, "allow");
			rep(param, '.', 'X');
			nvram_set(param, "allow");
			sprintf(param, "%s_macmode1", var);
			rep(param, '.', 'X');
			nvram_set(param, "other");
			sprintf(param, "%s_maclist", var);
			nvram_set(param, "A4:C0:E1:00:00:00/24");

			// dhcpd
			sprintf(param, "%s>On>20>200>60", var);
			nvram_set("mdhcpd", param);
			nvram_seti("mdhcpd_count", 1);
		}

	} else if (enabled == 1 && !strcmp(websGetVar(wp, "spotpass", "0"), "0")) {

		if (strcmp(nvram_default_get("spotpass_vif", ""), "")) {
			sprintf(var, "%s.%%d", prefix);
			int index = 0;
			if (sscanf(nvram_get("spotpass_vif"), var, &index) == 1) {
				sprintf(var, "%s", nvram_get("spotpass_vif"));
				int count = get_vifcount(prefix);
				int index = var[strlen(var) - 1] - '0';
				while (get_vifcount(prefix) >= index) {
					remove_vifs_single(prefix);
				}
				nvram_set("spotpass_vif", "");

				nvram_set("mdhcpd", "");
				nvram_seti("mdhcpd_count", 0);
			}
		}
	}

	if (websGetVari(wp, "spotpass", 0) != enabled) {
		addAction("wireless");
		nvram_seti("nowebaction", 1);
	}

	nvram_set("spotpass", websGetVar(wp, "spotpass", "0"));

	char *value = websGetVar(wp, "action", "");

	//addAction("spotpass_start");
	applytake(value);
}
#endif
