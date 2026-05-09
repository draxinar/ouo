CC=gcc
LD=gcc

GIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)

CFLAGS+=-Wall -Wextra -Wpedantic -O0 -g -std=c99 -pthread -D_POSIX_C_SOURCE=200809L
CFLAGS+=-DOUO_VERSION='"$(GIT_VERSION)"'
CFLAGS+=$(EXTRA_CFLAGS)
LDFLAGS+=-pthread -lm

ifdef M32
CFLAGS+=-m32
LDFLAGS+=-m32
endif

ifdef ASAN
CFLAGS+=-fsanitize=address
LDFLAGS+=-fsanitize=address
endif

ifdef VALGRIND
CFLAGS+=-DVALGRIND
endif

ifdef WERROR
CFLAGS+=-Werror
endif

TARG=ouo

OFILES=\
	account.o\
	anim.o\
	bankdefs.o\
	bboard.o\
	blockmanager.o\
	blowfish.o\
	book.o\
	combat.o\
	config.o\
	container.o\
	containerhandle.o\
	convo.o\
	corpse.o\
	cstring.o\
	defcon.o\
	dice.o\
	dynamic.o\
	egg.o\
	entity.o\
	entitymanager.o\
	entitymap.o\
	feature.o\
	feistel.o\
	filemanager.o\
	fns.o\
	gamecentmon.o\
	gm_player_menu.o\
	gmedit.o\
	help_queue.o\
	huffman.o\
	io.o\
	item.o\
	list.o\
	listensocket.o\
	load.o\
	location.o\
	log.o\
	magicfactory.o\
	magiclist.o\
	main.o\
	mobile.o\
	multi.o\
	nodepool.o\
	npc.o\
	objvar.o\
	packet_handler.o\
	packet_manager.o\
	packet_utils.o\
	pending_auth.o\
	player.o\
	random.o\
	region.o\
	res.o\
	resbank.o\
	resource_entity.o\
	resource_regrowth.o\
	resquery.o\
	scommand.o\
	serial_list.o\
	sha256.o\
	shopkeeper.o\
	signpost.o\
	skill.o\
	socket.o\
	stddeque.o\
	stdlist.o\
	stdptrlist.o\
	stl.o\
	streambuf.o\
	taglist.o\
	template.o\
	terrain.o\
	time.o\
	timer.o\
	trade.o\
	twofish.o\
	usersock.o\
	ustring.o\
	utils.o\
	version.o\
	vtable.o\
	watchdog.o\
	weapon.o\
	weather.o\
	wombat.o\
	wombat_compile.o\
	wombat_escript.o\
	wombat_exec.o\
	wombat_stl.o\
	world.o\

HFILES=\
	account.h\
	anim.h\
	bankdefs.h\
	bboard.h\
	blockmanager.h\
	blowfish.h\
	book.h\
	channel.h\
	combat.h\
	config.h\
	container.h\
	containerhandle.h\
	convo.h\
	corpse.h\
	cstring.h\
	dat.h\
	defcon.h\
	dice.h\
	dynamic.h\
	egg.h\
	entity.h\
	entitymanager.h\
	entitymap.h\
	feature.h\
	feistel.h\
	filemanager.h\
	fns.h\
	gamecentmon.h\
	gm_names.h\
	gm_player_menu.h\
	gmedit.h\
	help_queue.h\
	huffman.h\
	io.h\
	item.h\
	list.h\
	listensocket.h\
	load.h\
	location.h\
	log.h\
	magicfactory.h\
	magiclist.h\
	main.h\
	mobile.h\
	multi.h\
	nodepool.h\
	npc.h\
	objvar.h\
	packet_handler.h\
	packet_manager.h\
	packet_utils.h\
	pending_auth.h\
	player.h\
	random.h\
	region.h\
	res.h\
	resbank.h\
	resource_entity.h\
	resquery.h\
	scommand.h\
	serial_list.h\
	sha256.h\
	shopkeeper.h\
	signpost.h\
	skill.h\
	socket.h\
	stddeque.h\
	stdlist.h\
	stdptrlist.h\
	stl.h\
	streambuf.h\
	taglist.h\
	template.h\
	terrain.h\
	time.h\
	timer.h\
	trade.h\
	twofish.h\
	usersock.h\
	ustring.h\
	utils.h\
	version.h\
	vg_pool.h\
	vtable.h\
	watchdog.h\
	weapon.h\
	weather.h\
	wombat.h\
	wombat_compile.h\
	wombat_escript.h\
	wombat_exec.h\
	wombat_stl.h\
	world.h\

.PHONY: all clean install format

all: $(TARG)

$(TARG): $(OFILES) $(HFILES)
	$(LD) $(LDFLAGS) -o $(TARG) $(OFILES)

%.o: %.c $(HFILES)
	$(CC) -c $(CFLAGS) $*.c

clean:
	rm -f *.o ouo

install: $(TARG)
	install -d $(DESTDIR)/opt/ouo/run
	install -m 755 $(TARG) $(DESTDIR)/opt/ouo/run/$(TARG)
	install -m 755 systemd/ouo-status.sh $(DESTDIR)/opt/ouo/ouo-status.sh
	install -m 755 systemd/ouo-coredump.sh $(DESTDIR)/opt/ouo/ouo-coredump.sh
	install -m 755 systemd/ouo-backup.sh $(DESTDIR)/opt/ouo/ouo-backup.sh
	install -d $(DESTDIR)/usr/lib/systemd/system
	install -m 644 systemd/ouo.service $(DESTDIR)/usr/lib/systemd/system/
	install -m 644 systemd/ouo-coredump@.service $(DESTDIR)/usr/lib/systemd/system/
	install -m 644 systemd/ouo-status.service $(DESTDIR)/usr/lib/systemd/system/
	install -m 644 systemd/ouo-status.timer $(DESTDIR)/usr/lib/systemd/system/
	install -m 644 systemd/ouo-backup.service $(DESTDIR)/usr/lib/systemd/system/
	install -m 644 systemd/ouo-backup.timer $(DESTDIR)/usr/lib/systemd/system/
	install -d $(DESTDIR)/usr/share/polkit-1/rules.d
	install -m 644 systemd/50-ouo.rules $(DESTDIR)/usr/share/polkit-1/rules.d/
	install -d $(DESTDIR)/etc/default
	test -f $(DESTDIR)/etc/default/ouo || install -m 644 systemd/ouo.default $(DESTDIR)/etc/default/ouo

format:
	clang-format -i *.c *.h

cppcheck:
	cppcheck --enable=warning,performance,portability --error-exitcode=1 \
		--suppressions-list=.cppcheck-suppressions --std=c99 -q *.c

clang-analyze:
	scripts/run-clang-analyzer.sh
