all: vncclient.a vncserver.a

vncclient.a:
	$(MAKE) -s -C ./libvncclient

vncserver.a:
	$(MAKE) -s -C ./libvncserver

clean:
	@$(MAKE) -s -C ./libvncclient clean
	@$(MAKE) -s -C ./libvncserver clean
	@rm -f vncclient.a vncserver.a
