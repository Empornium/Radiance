#####
# requires locustio and python-bencode from pip
####

import random
import string
import socket
import struct
import ConfigParser
from locust import HttpLocust, TaskSet, task
from bencode import Bencoder

def id_generator(size=32, chars=string.ascii_lowercase + string.digits):
	return ''.join(random.choice(chars) for _ in range(size))

def new_peer(client):
	gen_torrent = random.random()
	if (gen_torrent >= 0.8 or len(torrents) == 0):
                torrent = Torrent(client)
		torrents[torrent.id] = torrent
	else:
		torrent = torrents[random.choice(torrents.keys())]

	gen_user = random.random()
	if (gen_user >= 0.8 or len(users) == 0):
                user = User(client)
		users[user.id] = user
	else:
		user = users[random.choice(users.keys())]

	return Peer(client, user, torrent)

class Torrent():
	def __init__(self, client):
		self.info_hash  = id_generator(20, '0123456789abcdef')
		self.id         = random.randint(0, 10000)
		self.size       = random.randint(1, 65355)
		if debug:
			print "Adding torrent size: %s" % (self.size)
		client.get("/%s/update?action=add_torrent&id=%d&info_hash=%s&freetorrent=0" % (tracker_secret, self.id, self.info_hash), name="Add Torrent")

class User():
	def __init__(self, client):
		self.passkey    = id_generator()
		self.id         = random.randint(0, 10000)
		self.ip         = socket.inet_ntoa(struct.pack('!L', random.randint(0, 0xffffffff)))
		self.port       = random.randint(5000, 65535)
		if debug:
			print "Adding user %d with passkey %s" % (self.id, self.passkey)
		client.get("/%s/update?action=add_user&id=%d&passkey=%s" % (tracker_secret, self.id, self.passkey), name="Add User")

class Peer():
	def __init__(self, client, user, torrent):
                self.user         = user
                self.torrent      = torrent
		self.left         = torrent.size
		self.peer_id      = "-LCST-%s" % (id_generator(14))
		self.uploaded     = 0
		self.downloaded   = 0
		self.corrupt      = 0
		self.numwant      = 50
	        self.started      = False
        	self.completed    = False
	        self.stopped      = False
		self.interval     = 0
		self.min_interval = 0
		self.announce(client)

	def announce(self, client):
		event = ''
		if not(self.started):
			event = 'event=started'
			self.started = True
		else:
			downloaded       = random.randint(0, self.left)
			self.downloaded += downloaded
			self.uploaded   += random.randint(0, self.downloaded)
			self.left       -= downloaded

			if (self.left == 0 and not(self.completed)):
				event = 'event=completed'
                                self.completed = True

               	with client.get("/%s/announce?info_hash=%s&peer_id=%s&ip=%s&port=%d&downloaded=%s&uploaded=%s&left=%s&%s&compact=1" % (self.user.passkey, self.torrent.info_hash, self.peer_id, self.user.ip, self.user.port, self.downloaded, self.uploaded, self.left, event), name="Peer Announce", catch_response=True) as resp:
			if debug:
				print "Announcing user %d with passkey %s" % (self.user.id, self.user.passkey)
#			if resp.status_code == 200:
#				print resp.content
#				tracker_resp      = Bencoder.decode(resp.content)
#				if debug:
#					print tracker_resp
#				if 'peers' in tracker_resp:
#					resp.success()
#					self.interval     = int(tracker_resp['interval'])
#					self.min_interval = int(tracker_resp['min interval'])
#				else:
#					resp.failure(tracker_resp['failure reason'])
#
#			else:
#				resp.failure("No response")

#	def __del__(self):
#		self.stopped = True
#		self.announce()

config = ConfigParser.SafeConfigParser()
config.read('radiance.conf')
tracker_secret = config.get('tracker', 'site_password')
debug          = config.getboolean('tracker', 'readonly')

users    = dict()
torrents = dict()
peers    = dict()

class PeerTaskSet(TaskSet):
	def on_start(self):
                self.peer = new_peer(self.client)
		self.min_wait = self.peer.min_interval*1000
		self.max_wait = self.peer.interval*1000

	@task(30)
	def peer_announce(self):
                self.peer.announce(self.client)

#	@task(1)
#	def peer_stopped(self):
#		self.interrupt()

class PeerLocust(HttpLocust):
	task_set = PeerTaskSet
