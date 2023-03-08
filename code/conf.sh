bash configure \
	--with-boot-jdk=~/jdk-19.0.2+7 \
	--with-jtreg=../jtreg/build/images/jtreg/ \
	--with-gtest=../googletest \
	--with-hsdis=binutils \
	--with-binutils-src=~/binutils-2.37/ \
	--with-debug-level=fastdebug \
	--with-native-debug-symbols=internal \
	--disable-precompiled-headers \
	--with-num-cores=8
