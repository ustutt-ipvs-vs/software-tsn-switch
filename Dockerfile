# The repo's CI/CD pipeline uses an image built with this Dockerfile to build & test

FROM debian
WORKDIR /root/enpro-switch

COPY yang yang-models
RUN apt-get update && \
    # APT dependencies
    apt install -y build-essential libpcre2-dev libssl-dev libssh-dev \
    libcurl4-openssl-dev systemd-dev libsystemd-dev liblldpctl-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc \
    libspdlog-dev nodejs npm chrony git curl cmake \
    clang-format clang-tidy python3 python3-pip doxygen graphviz

# Install from repositories
RUN mkdir gitrepos && cd gitrepos && \
    # Libyang
    mkdir libyang && cd libyang && \
    curl -L https://github.com/CESNET/libyang/archive/7dcf6b1e0172e79fba1f8275304e03ffafd58b9b.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake .. && make && make install && ldconfig && \
    cd ../.. && \
    # Libnetconf2
    mkdir libnetconf2 && cd libnetconf2 && \
    curl -L https://github.com/CESNET/libnetconf2/archive/84c60d815a10a4f9a99f6c003f7232808eb8ac7a.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake .. && make && make install && ldconfig && \
    cd ../.. && \
    # Sysrepo
    mkdir sysrepo && cd sysrepo && \
    curl -L https://github.com/sysrepo/sysrepo/archive/1a9e66c60e53333f8443188a5e5c9d13153f3de2.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake .. && make && make install && ldconfig && \
    cd ../.. && \
    # Netopeer2
    mkdir netopeer2 && cd netopeer2 && \
    curl -L https://github.com/CESNET/netopeer2/archive/616ec7b6912c2381a6c33a3a1183d53d4825bb91.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake .. && make && make install && ldconfig && \
    cd ../.. && \
    # Libyang-cpp
    mkdir libyang-cpp && cd libyang-cpp && \
    curl -L https://github.com/CESNET/libyang-cpp/archive/1fd11bdd1bd02c92477fb07bfd579994721862cf.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake -DBUILD_TESTING=off .. && \
    make && make install && ldconfig && \
    cd ../.. && \
    # Libnetconf2-cpp
    mkdir libnetconf2-cpp && cd libnetconf2-cpp && \
    curl -L https://github.com/CESNET/libnetconf2-cpp/archive/2d007b7ecbf65adec24a927014771cae9cedb368.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake -DBUILD_TESTING=off .. && \
    make && make install && ldconfig && \
    cd ../.. && \
    # Sysrepo-cpp
    mkdir sysrepo-cpp && cd sysrepo-cpp && \
    curl -L https://github.com/sysrepo/sysrepo-cpp/archive/60d729e4791e5447cf0d969be6da9f8f1878af64.tar.gz | \
    tar xz --strip-components=1 && \
    mkdir build && cd build && cmake -DBUILD_TESTING=off .. && \
    make && make install && ldconfig && \
    cd ../.. && \
    # Linuxptp
    mkdir linuxptp && cd linuxptp && \
    curl -L https://github.com/richardcochran/linuxptp/archive/ddeec0f0adb3732756f98895a39bfee06a3a9827.tar.gz | \
    tar xz --strip-components=1 && \
    make && make install && \
    cd .. && \
    # Delete leftover directories
    rm -rf libyang libnetconf2 netopeer2 libyang-cpp libnetconf2-cpp sysrepo-cpp

    # Yang models
RUN useradd --system --shell /usr/sbin/nologin netconf-api -p 'private' && \
    cd yang-models && \
    sysrepoctl -i example-demo.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -c ietf-interfaces -o root -g netconf-api -p 660 && \
    sysrepoctl -i iana-if-type@2023-01-26.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-types.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1q-types.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1q-bridge.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1q-sched.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1q-sched-bridge.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ietf-routing@2018-03-13.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1ab-types.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee802-dot1ab-lldp.yang -o root -g netconf-api -p 660 && \
    sysrepoctl -i ieee1588-ptp-tt.yang  -o root -g netconf-api -p 660 -e performance-monitoring && \
    sysrepoctl -i ieee802-dot1as-gptp.yang  -o root -g netconf-api -p 660 && \
    # Netconf configuration
    cd .. && \
    echo '<nacm xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-acm"><enable-nacm>true</enable-nacm><read-default>permit</read-default><write-default>deny</write-default><groups><group><name>admin</name><user-name>root</user-name><user-name>netconf-api</user-name></group></groups><rule-list><name>admin-full-access</name><group>admin</group><rule><name>permit-all</name><module-name>*</module-name><access-operations>*</access-operations><action>permit</action></rule></rule-list></nacm>' > nacm_init.xml && \
    sysrepocfg --import=nacm_init.xml --module ietf-netconf-acm --datastore running && \
    sysrepocfg --copy-from running --datastore startup

# Install dependencies for sphinx builds
COPY doc/user/requirements.txt requirements.txt
RUN pip install --break-system-packages --no-cache-dir -r requirements.txt
