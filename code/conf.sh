bash configure \
	--with-boot-jdk=~/jdk-20+36 \
	--with-jtreg=../jtreg/build/images/jtreg/ \
	--with-gtest=../googletest \
	--with-hsdis=binutils \
	--with-binutils-src=~/binutils-2.37/ \
	--with-debug-level=slowdebug \
	--with-native-debug-symbols=internal \
	--with-num-cores=8
