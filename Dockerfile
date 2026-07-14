FROM --platform=linux/arm64/v8 ubuntu:24.04
RUN mkdir /app
RUN apt-get update

## Install dependencies
RUN apt-get install -y make gcc bzip2
RUN apt-get install -y libusb-1.0-0-dev
# RUN apt-get install -y wget telnet

# Copy files
COPY . /app/zwo

## ASI SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf ASI_linux_mac_SDK_V1.41.tar.bz2 && cd ASI_linux_mac_SDK_V1.41/lib
RUN cp /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.41/lib/armv8/libASICamera2.so.1.41 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.41/lib/ && install asi.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libASICamera2.so.1.41 libASICamera2.so
## EFW SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf EFW_linux_mac_SDK_V1.7.tar.bz2 && cd EFW_linux_mac_SDK_V1.7/lib
RUN cp /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/armv8/libEFWFilter.so.1.7 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/ && install efw.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libEFWFilter.so.1.7 libEFWFilter.so
## build
RUN cd /app/zwo/src/server && make clean && make

## install
RUN cp /app/zwo/src/server/zwoserver /usr/local/bin

# copy service files
RUN mkdir -p /etc/systemd/system/ && cp /app/zwo/etc/systemd/system/zwo.service /etc/systemd/system/

# Make a self-extracting installer
## deps
RUN apt-get install -y wget rsync jq
RUN wget -q -O - https://api.github.com/repos/megastep/makeself/releases/latest |  jq -r '.assets[] | select(.name | contains ("run")) | .browser_download_url' | wget -i - -O /app/makeself.run 
RUN chmod +x /app/makeself.run && cd /app && /app/makeself.run

RUN rsync -avR /usr/local/lib/ /usr/local/bin/ /lib/udev/rules.d/asi.rules /lib/udev/rules.d/efw.rules /etc/systemd/system/zwo.service /app/installer/
RUN chmod -x /app/installer/lib/udev/rules.d/asi.rules
RUN chmod -x /app/installer/lib/udev/rules.d/efw.rules
RUN mkdir /app/installer/tmp && cp /app/zwo/etc/scripts/install_zwoserver.sh /app/installer/tmp/
RUN /app/makeself*/makeself.sh --needroot --target / /app/installer /app/zwoserver.run "ZWO Server" /tmp/install_zwoserver.sh

        

# Run
# EXPOSE 52311
# ENV LD_LIBRARY_PATH=/usr/local/lib/
# CMD ["/app/zwo/src/server/zwoserver"]
