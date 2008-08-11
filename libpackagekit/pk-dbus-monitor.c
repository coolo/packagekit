/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>

#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-dbus-monitor.h"

static void     pk_dbus_monitor_class_init	(PkDbusMonitorClass	*klass);
static void     pk_dbus_monitor_init		(PkDbusMonitor		*dbus_monitor);
static void     pk_dbus_monitor_finalize	(GObject		*object);

#define PK_DBUS_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DBUS_MONITOR, PkDbusMonitorPrivate))

struct PkDbusMonitorPrivate
{
	PkDbusMonitorType	 bus_type;
	gchar			*service;
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	const gchar		*unique_name;
};

enum {
	PK_DBUS_MONITOR_CONNECTION_CHANGED,
	PK_DBUS_MONITOR_CONNECTION_REPLACED,
	PK_DBUS_MONITOR_LAST_SIGNAL
};

static guint signals [PK_DBUS_MONITOR_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkDbusMonitor, pk_dbus_monitor, G_TYPE_OBJECT)

/**
 * pk_dbus_monitor_name_owner_changed_cb:
 **/
static void
pk_dbus_monitor_name_owner_changed_cb (DBusGProxy *proxy, const gchar *name,
				       const gchar *prev, const gchar *new,
				       PkDbusMonitor *monitor)
{
	guint new_len;
	guint prev_len;

	g_return_if_fail (PK_IS_DBUS_MONITOR (monitor));
	if (monitor->priv->proxy == NULL) {
		return;
	}

	/* not us */
	if (strcmp (name, monitor->priv->service) != 0) {
		return;
	}

	/* ITS4: ignore, not used for allocation */
	new_len = strlen (new);
	/* ITS4: ignore, not used for allocation */
	prev_len = strlen (prev);

	/* something --> nothing */
	if (prev_len != 0 && new_len == 0) {
		g_signal_emit (monitor, signals [PK_DBUS_MONITOR_CONNECTION_CHANGED], 0, FALSE);
		return;
	}

	/* nothing --> something */
	if (prev_len == 0 && new_len != 0) {
		g_signal_emit (monitor, signals [PK_DBUS_MONITOR_CONNECTION_CHANGED], 0, TRUE);
		return;
	}

	/* something --> something (we've replaced the old process) */
	if (prev_len != 0 && new_len != 0) {
		/* only send this to the prev client */
		if (strcmp (monitor->priv->unique_name, prev) == 0) {
			g_signal_emit (monitor, signals [PK_DBUS_MONITOR_CONNECTION_REPLACED], 0);
		}
		return;
	}
}

/**
 * pk_dbus_monitor_assign:
 * @pk_dbus_monitor: This class instance
 * @bus_type: The bus type, either PK_DBUS_MONITOR_SESSION or PK_DBUS_MONITOR_SYSTEM
 * @service: The PK_DBUS_MONITOR service name
 * Return value: success
 *
 * Emits connection-changed(TRUE) if connection is alive - this means you
 * have to connect up the callback before this function is called.
 **/
gboolean
pk_dbus_monitor_assign (PkDbusMonitor *monitor, PkDbusMonitorType bus_type, const gchar *service)
{
	GError *error = NULL;
	gboolean connected;
	DBusConnection *conn;

	g_return_val_if_fail (PK_IS_DBUS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (service != NULL, FALSE);

	if (monitor->priv->proxy != NULL) {
		pk_warning ("already assigned!");
		return FALSE;
	}

	monitor->priv->service = g_strdup (service);
	monitor->priv->bus_type = bus_type;

	/* connect to correct bus */
	if (bus_type == PK_DBUS_MONITOR_SESSION) {
		monitor->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	} else {
		monitor->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	}
	if (error != NULL) {
		pk_warning ("Cannot connect to bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	monitor->priv->proxy = dbus_g_proxy_new_for_name_owner (monitor->priv->connection,
								DBUS_SERVICE_DBUS,
								DBUS_PATH_DBUS,
						 		DBUS_INTERFACE_DBUS,
								&error);
	if (error != NULL) {
		pk_warning ("Cannot connect to DBUS: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	dbus_g_proxy_add_signal (monitor->priv->proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (monitor->priv->proxy, "NameOwnerChanged",
				     G_CALLBACK (pk_dbus_monitor_name_owner_changed_cb),
				     monitor, NULL);

	/* coldplug */
	connected = pk_dbus_monitor_is_connected (monitor);
	if (connected) {
		g_signal_emit (monitor, signals [PK_DBUS_MONITOR_CONNECTION_CHANGED], 0, TRUE);
	}

	/* save this for the replaced check */
	conn = dbus_g_connection_get_connection (monitor->priv->connection);
	monitor->priv->unique_name = dbus_bus_get_unique_name (conn);
	return TRUE;
}

/**
 * pk_dbus_monitor_is_connected:
 * @pk_dbus_monitor: This class instance
 * Return value: if we are connected to a valid watch
 **/
gboolean
pk_dbus_monitor_is_connected (PkDbusMonitor *monitor)
{
	DBusError error;
	DBusConnection *conn;
	gboolean ret;
	g_return_val_if_fail (PK_IS_DBUS_MONITOR (monitor), FALSE);

	/* get raw connection */
	conn = dbus_g_connection_get_connection (monitor->priv->connection);
	dbus_error_init (&error);
	ret = dbus_bus_name_has_owner (conn, monitor->priv->service, &error);
	if (dbus_error_is_set (&error)) {
		pk_debug ("error: %s", error.message);
		dbus_error_free (&error);
	}

	return ret;
}

/**
 * pk_dbus_monitor_class_init:
 * @klass: The PkDbusMonitorClass
 **/
static void
pk_dbus_monitor_class_init (PkDbusMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_dbus_monitor_finalize;
	g_type_class_add_private (klass, sizeof (PkDbusMonitorPrivate));
	signals [PK_DBUS_MONITOR_CONNECTION_CHANGED] =
		g_signal_new ("connection-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkDbusMonitorClass, connection_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_DBUS_MONITOR_CONNECTION_REPLACED] =
		g_signal_new ("connection-replaced",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkDbusMonitorClass, connection_replaced),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_dbus_monitor_init:
 * @dbus_monitor: This class instance
 **/
static void
pk_dbus_monitor_init (PkDbusMonitor *monitor)
{
	monitor->priv = PK_DBUS_MONITOR_GET_PRIVATE (monitor);
	monitor->priv->service = NULL;
	monitor->priv->bus_type = PK_DBUS_MONITOR_SESSION;
	monitor->priv->proxy = NULL;
}

/**
 * pk_dbus_monitor_finalize:
 * @object: The object to finalize
 **/
static void
pk_dbus_monitor_finalize (GObject *object)
{
	PkDbusMonitor *monitor;

	g_return_if_fail (PK_IS_DBUS_MONITOR (object));

	monitor = PK_DBUS_MONITOR (object);

	g_return_if_fail (monitor->priv != NULL);
	if (monitor->priv->proxy != NULL) {
		g_object_unref (monitor->priv->proxy);
	}

	G_OBJECT_CLASS (pk_dbus_monitor_parent_class)->finalize (object);
}

/**
 * pk_dbus_monitor_new:
 *
 * Return value: a new PkDbusMonitor object.
 **/
PkDbusMonitor *
pk_dbus_monitor_new (void)
{
	PkDbusMonitor *monitor;
	monitor = g_object_new (PK_TYPE_DBUS_MONITOR, NULL);
	return PK_DBUS_MONITOR (monitor);
}
