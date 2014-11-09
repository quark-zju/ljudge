default:
	make -C src

install:
	make -C src install

clean:
	make -C src clean

deb:
	debuild -i -us -uc -b
