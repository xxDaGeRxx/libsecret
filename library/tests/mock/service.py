#!/usr/bin/env python

#
# Copyright 2011 Stef Walter
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation; either version 2 of the licence or (at
# your option) any later version.
#
# See the included COPYING file for more information.
#

import getopt
import os
import sys
import time
import unittest

import aes
import dh
import hkdf

import dbus
import dbus.service
import dbus.glib
import gobject

COLLECTION_PREFIX = "/org/freedesktop/secrets/collection/"

bus_name = 'org.freedesktop.Secret.MockService'
ready_pipe = -1
objects = { }

class NotSupported(dbus.exceptions.DBusException):
	def __init__(self, msg):
		dbus.exceptions.DBusException.__init__(self, msg, name="org.freedesktop.DBus.Error.NotSupported")

class InvalidArgs(dbus.exceptions.DBusException):
	def __init__(self, msg):
		dbus.exceptions.DBusException.__init__(self, msg, name="org.freedesktop.DBus.Error.InvalidArgs")

class IsLocked(dbus.exceptions.DBusException):
	def __init__(self, msg):
		dbus.exceptions.DBusException.__init__(self, msg, name="org.freedesktop.Secret.Error.IsLocked")

unique_identifier = 0
def next_identifier():
	global unique_identifier
	unique_identifier += 1
	return unique_identifier

def hex_encode(string):
	return "".join([hex(ord(c))[2:].zfill(2) for c in string])


class PlainAlgorithm():
	def negotiate(self, service, sender, param):
		if type (param) != dbus.String:
			raise InvalidArgs("invalid argument passed to OpenSession")
		session = SecretSession(service, sender, self, None)
		return (dbus.String("", variant_level=1), session)

	def encrypt(self, key, data):
		return ("", data)


class AesAlgorithm():
	def negotiate(self, service, sender, param):
		if type (param) != dbus.ByteArray:
			raise InvalidArgs("invalid argument passed to OpenSession")
		privat, publi = dh.generate_pair()
		peer = dh.bytes_to_number(param)
		# print "mock publi: ", hex(publi)
		# print " mock peer: ", hex(peer)
		ikm = dh.derive_key(privat, peer)
		# print "  mock ikm: ", hex_encode(ikm)
		key = hkdf.hkdf(ikm, 16)
		# print "  mock key: ", hex_encode(key)
		session = SecretSession(service, sender, self, key)
		return (dbus.ByteArray(dh.number_to_bytes(publi), variant_level=1), session)

	def encrypt(self, key, data):
		key = map(ord, key)
		data = aes.append_PKCS7_padding(data)
		keysize = len(key)
		iv = [ord(i) for i in os.urandom(16)]
		mode = aes.AESModeOfOperation.modeOfOperation["CBC"]
		moo = aes.AESModeOfOperation()
		(mode, length, ciph) = moo.encrypt(data, mode, key, keysize, iv)
		return ("".join([chr(i) for i in iv]),
		        "".join([chr(i) for i in ciph]))


class SecretPrompt(dbus.service.Object):
	def __init__(self, service, sender, prompt_name=None, delay=0,
	             dismiss=False, result=dbus.String("", variant_level=1),
	             action=None):
		self.sender = sender
		self.service = service
		self.delay = 0
		self.dismiss = False
		self.result = result
		self.action = action
		self.completed = False
		if prompt_name:
			self.path = "/org/freedesktop/secrets/prompts/%s" % prompt_name
		else:
			self.path = "/org/freedesktop/secrets/prompts/p%d" % next_identifier()
		dbus.service.Object.__init__(self, service.bus_name, self.path)
		service.add_prompt(self)
		assert self.path not in objects
		objects[self.path] = self

	def _complete(self):
		if self.completed:
			return
		self.completed = True
		self.Completed(self.dismiss, self.result)
		self.remove_from_connection()

	@dbus.service.method('org.freedesktop.Secret.Prompt')
	def Prompt(self, window_id):
		if self.action:
			self.action()
		gobject.timeout_add(self.delay * 1000, self._complete)

	@dbus.service.method('org.freedesktop.Secret.Prompt')
	def Dismiss(self):
		self._complete()

	@dbus.service.signal(dbus_interface='org.freedesktop.Secret.Prompt', signature='bv')
	def Completed(self, dismiss, result):
		pass


class SecretSession(dbus.service.Object):
	def __init__(self, service, sender, algorithm, key):
		self.sender = sender
		self.service = service
		self.algorithm = algorithm
		self.key = key
		self.path = "/org/freedesktop/secrets/sessions/%d" % next_identifier()
		dbus.service.Object.__init__(self, service.bus_name, self.path)
		service.add_session(self)
		objects[self.path] = self

	def encode_secret(self, secret, content_type):
		(params, data) = self.algorithm.encrypt(self.key, secret)
		# print "   mock iv: ", hex_encode(params)
		# print " mock ciph: ", hex_encode(data)
		return dbus.Struct((dbus.ObjectPath(self.path), dbus.ByteArray(params),
		                    dbus.ByteArray(data), dbus.String(content_type)),
		                   signature="oayays")

	@dbus.service.method('org.freedesktop.Secret.Session')
	def Close(self):
		self.remove_from_connection()
		self.service.remove_session(self)


class SecretItem(dbus.service.Object):
	def __init__(self, collection, identifier, label="Item", attributes={ },
	             secret="", confirm=False, content_type="text/plain"):
		self.collection = collection
		self.identifier = identifier
		self.label = label
		self.secret = secret
		self.attributes = attributes
		self.content_type = content_type
		self.path = "%s/%s" % (collection.path, identifier)
		self.confirm = confirm
		self.created = self.modified = time.time()
		dbus.service.Object.__init__(self, collection.service.bus_name, self.path)
		collection.items[identifier] = self
		objects[self.path] = self

	def match_attributes(self, attributes):
		for (key, value) in attributes.items():
			if not self.attributes.get(key) == value:
				return False
		return True

	def get_locked(self):
		return self.collection.locked

	def perform_xlock(self, lock):
		return self.collection.perform_xlock(lock)

	def perform_delete(self):
		del self.collection.items[self.identifier]
		del objects[self.path]
		self.remove_from_connection()

	@dbus.service.method('org.freedesktop.Secret.Item', sender_keyword='sender')
	def GetSecret(self, session_path, sender=None):
		session = objects.get(session_path, None)
		if not session or session.sender != sender:
			raise InvalidArgs("session invalid: %s" % session_path) 
		if self.get_locked():
			raise IsLocked("secret is locked: %s" % self.path)
		return session.encode_secret(self.secret, self.content_type)

	@dbus.service.method('org.freedesktop.Secret.Item', sender_keyword='sender')
	def Delete(self, sender=None):
		if self.confirm:
			prompt = SecretPrompt(self.collection.service, sender,
			                      dismiss=False, action=self.perform_delete)
			return dbus.ObjectPath(prompt.path)
		else:
			self.perform_delete()
			return dbus.ObjectPath("/")

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
	def Get(self, interface_name, property_name):
		return self.GetAll(interface_name)[property_name]

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
	def GetAll(self, interface_name):
		if interface_name == 'org.freedesktop.Secret.Item':
			return {
				'Locked': self.get_locked(),
				'Attributes': dbus.Dictionary(self.attributes, signature='ss'),
				'Label': self.label,
				'Created': dbus.UInt64(self.created),
				'Modified': dbus.UInt64(self.modified)
			}
		else:
			raise InvalidArgs('Unknown %s interface' % interface_name)

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ssv')
	def Set(self, interface_name, property_name, new_value):
		if interface_name != 'org.freedesktop.Secret.Item':
			raise InvalidArgs('Unknown %s interface' % interface_name)
		if property_name == "Label":
			self.label = str(new_value)
		elif property_name == "Attributes":
			self.attributes = dict(new_value)
		else:
			raise InvalidArgs('Unknown %s interface' % property_name)
		self.PropertiesChanged(interface_name, { property_name: new_value }, [])

	@dbus.service.signal(dbus.PROPERTIES_IFACE, signature='sa{sv}as')
	def PropertiesChanged(self, interface_name, changed_properties, invalidated_properties):
		self.modified = time.time()


class SecretCollection(dbus.service.Object):
	def __init__(self, service, identifier, label="Collection", locked=False, confirm=False):
		self.service = service
		self.identifier = identifier
		self.label = label
		self.locked = locked
		self.items = { }
		self.confirm = confirm
		self.created = self.modified = time.time()
		self.path = "%s%s" % (COLLECTION_PREFIX, identifier)
		dbus.service.Object.__init__(self, service.bus_name, self.path)
		service.collections[identifier] = self
		objects[self.path] = self

	def search_items(self, attributes):
		results = []
		for item in self.items.values():
			if item.match_attributes(attributes):
				results.append(item)
		return results

	def get_locked(self):
		return self.locked

	def perform_xlock(self, lock):
		self.locked = lock
		for item in self.items.values():
			self.PropertiesChanged('org.freedesktop.Secret.Item', { "Locked" : lock }, [])
		self.PropertiesChanged('org.freedesktop.Secret.Collection', { "Locked" : lock }, [])

	def perform_delete(self):
		for item in self.items.values():
			item.perform_delete()
		del self.service.collections[self.identifier]
		del objects[self.path]
		self.remove_from_connection()

	@dbus.service.method('org.freedesktop.Secret.Collection', sender_keyword='sender')
	def Delete(self, sender=None):
		if self.confirm:
			prompt = SecretPrompt(self.collection.service, sender,
			                      dismiss=False, action=self.perform_delete)
			return dbus.ObjectPath(prompt.path)
		else:
			self.perform_delete()
			return dbus.ObjectPath("/")

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
	def Get(self, interface_name, property_name):
		return self.GetAll(interface_name)[property_name]

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
	def GetAll(self, interface_name):
		if interface_name == 'org.freedesktop.Secret.Collection':
			return {
				'Locked': self.get_locked(),
				'Label': self.label,
				'Created': dbus.UInt64(self.created),
				'Modified': dbus.UInt64(self.modified),
				'Items': dbus.Array([dbus.ObjectPath(i.path) for i in self.items.values()], signature='o')
			}
		else:
			raise InvalidArgs('Unknown %s interface' % interface_name)

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ssv')
	def Set(self, interface_name, property_name, new_value):
		if interface_name != 'org.freedesktop.Secret.Collection':
			raise InvalidArgs('Unknown %s interface' % interface_name)
		if property_name == "Label":
			self.label = str(new_value)
		else:
			raise InvalidArgs('Not a writable property %s' % property_name)
		self.PropertiesChanged(interface_name, { property_name: new_value }, [])

	@dbus.service.signal(dbus.PROPERTIES_IFACE, signature='sa{sv}as')
	def PropertiesChanged(self, interface_name, changed_properties, invalidated_properties):
		self.modified = time.time()


class SecretService(dbus.service.Object):

	algorithms = {
		'plain': PlainAlgorithm(),
		"dh-ietf1024-sha256-aes128-cbc-pkcs7": AesAlgorithm(),
	}

	def __init__(self, name=None):
		if name == None:
			name = bus_name
		bus = dbus.SessionBus()
		self.bus_name = dbus.service.BusName(name, allow_replacement=True, replace_existing=True)
		dbus.service.Object.__init__(self, self.bus_name, '/org/freedesktop/secrets')
		self.sessions = { }
		self.prompts = { }
		self.collections = { }

		def on_name_owner_changed(owned, old_owner, new_owner):
			if not new_owner:
				for session in list(self.sessions.get(old_owner, [])):
					session.Close()

		bus.add_signal_receiver(on_name_owner_changed,
		                        'NameOwnerChanged',
		                        'org.freedesktop.DBus')

	def add_standard_objects(self):
		collection = SecretCollection(self, "english", label="Collection One", locked=False)
		SecretItem(collection, "item_one", label="Item One", attributes={ "number": "1", "string": "one", "even": "false" }, secret="111")
		SecretItem(collection, "item_two", attributes={ "number": "2", "string": "two", "even": "true" }, secret="222")
		SecretItem(collection, "item_three", attributes={ "number": "3", "string": "three", "even": "false" }, secret="3333")

		collection = SecretCollection(self, "spanish", locked=True)
		SecretItem(collection, "item_one", attributes={ "number": "1", "string": "uno", "even": "false" }, secret="111")
		SecretItem(collection, "item_two", attributes={ "number": "2", "string": "dos", "even": "true" }, secret="222")
		SecretItem(collection, "item_three", attributes={ "number": "3", "string": "tres", "even": "false" }, secret="3333")
	
		collection = SecretCollection(self, "empty", locked=False)
	
	def listen(self):
		global ready_pipe
		loop = gobject.MainLoop()
		if ready_pipe >= 0:
			os.write(ready_pipe, "GO")
			os.close(ready_pipe)
			ready_pipe = -1
		loop.run()

	def add_session(self, session):
		if session.sender not in self.sessions:
			self.sessions[session.sender] = []
		self.sessions[session.sender].append(session)

	def remove_session(self, session):
		self.sessions[session.sender].remove(session)

	def add_prompt(self, prompt):
		if prompt.sender not in self.prompts:
			self.prompts[prompt.sender] = []
		self.prompts[prompt.sender].append(prompt)

	def remove_prompt (self, prompt):
		self.prompts[prompt.sender].remove(prompt)

	def find_item(self, object):
		if object.startswith(COLLECTION_PREFIX):
			parts = object[len(COLLECTION_PREFIX):].split("/", 1)
			if len(parts) == 2 and parts[0] in self.collections:
				return self.collections[parts[0]].get(parts[1], None)
		return None

	@dbus.service.method('org.freedesktop.Secret.Service', sender_keyword='sender')
	def Lock(self, paths, lock=True, sender=None):
		locked = []
		prompts = []
		for path in paths:
			if path not in objects:
				continue
			object = objects[path]
			if object.get_locked() == lock:
				locked.append(path)
			elif not object.confirm:
				object.perform_xlock(lock)
				locked.append(path)
			else:
				prompts.append(object)
		def prompt_callback():
			for object in prompts:
				object.perform_xlock(lock)
		locked = dbus.Array(locked, signature='o')
		if prompts:
			prompt = SecretPrompt(self, sender, dismiss=False, action=prompt_callback,
			                      result=dbus.Array([o.path for o in prompts], signature='o'))
			return (locked, dbus.ObjectPath(prompt.path))
		else:
			return (locked, dbus.ObjectPath("/"))

	@dbus.service.method('org.freedesktop.Secret.Service', sender_keyword='sender')
	def Unlock(self, paths, sender=None):
		return self.Lock(paths, lock=False, sender=sender)

	@dbus.service.method('org.freedesktop.Secret.Service', byte_arrays=True, sender_keyword='sender')
	def OpenSession(self, algorithm, param, sender=None):
		assert type(algorithm) == dbus.String

		if algorithm not in self.algorithms:
			raise NotSupported("algorithm %s is not supported" % algorithm)

		return self.algorithms[algorithm].negotiate(self, sender, param)

	@dbus.service.method('org.freedesktop.Secret.Service')
	def SearchItems(self, attributes):
		locked = [ ]
		unlocked = [ ]
		items = [ ]
		for collection in self.collections.values():
			items = collection.search_items(attributes)
			if collection.get_locked():
				locked.extend(items)
			else:
				unlocked.extend(items)
		return (dbus.Array(unlocked, "o"), dbus.Array(locked, "o"))

	@dbus.service.method('org.freedesktop.Secret.Service', sender_keyword='sender')
	def GetSecrets(self, item_paths, session_path, sender=None):
		session = objects.get(session_path, None)
		if not session or session.sender != sender:
			raise InvalidArgs("session invalid: %s" % session_path) 
		results = dbus.Dictionary(signature="o(oayays)")
		for item_path in item_paths:
			item = objects.get(item_path, None)
			if item and not item.get_locked():
				results[item_path] = item.GetSecret(session_path, sender)
		return results

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ss', out_signature='v')
	def Get(self, interface_name, property_name):
		return self.GetAll(interface_name)[property_name]

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='s', out_signature='a{sv}')
	def GetAll(self, interface_name):
		if interface_name == 'org.freedesktop.Secret.Service':
			return {
				'Collections': dbus.Array([dbus.ObjectPath(c.path) for c in self.collections.values()], signature='o')
			}
		else:
			raise InvalidArgs('Unknown %s interface' % interface_name)

	@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ssv')
	def Set(self, interface_name, property_name, new_value):
		if interface_name != 'org.freedesktop.Secret.Collection':
			raise InvalidArgs('Unknown %s interface' % interface_name)
		raise InvalidArgs('Not a writable property %s' % property_name)


def parse_options(args):
	global bus_name, ready_pipe
	try:
		opts, args = getopt.getopt(args, "nr", ["name=", "ready="])
	except getopt.GetoptError, err:
		print str(err)
		sys.exit(2)
	for o, a in opts:
		if o in ("-n", "--name"):
			bus_name = a
		elif o in ("-r", "--ready"):
			ready_pipe = int(a)
		else:
			assert False, "unhandled option"
	return args

parse_options(sys.argv[1:])
