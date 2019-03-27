## PVM Setup with Apache v2.2.11
This assumes the bare metal Ubuntu 16.04 Xen environment is already setup.

### Create and boot protected VM (PVM) on Dom0
    # create new Xen instance for PVM
    $ sudo xen-create-image \
          --genpass=0 \
          --password=root \
          --hostname=xd3-pvm2 \
          --dhcp \
          --dir=$HOME \
          --dist=xenial \
          --vcpus=1 \
          --memory=2048MB \
          --size=20G \
          | tee create-guest.log

    # load the loopback driver (if using --dir option above)
    $ sudo modprobe loop max_loop=255

    # boot the PVM guest
    $ sudo xl create /etc/xen/xd3-pvm2.cfg

    # obtain the PVM guest console (login as user "root" with password "root")
    $ sudo xl console xd3-pvm2

        # check kernel version (mwcomms driver requires version 4.4.x)
        $ uname -r

        # create non-root user
        $ adduser pvm
        $ usermod -aG sudo pvm

        # install additional software
        $ apt-get update ; apt-get dist-upgrade -y
        $ apt-get install make gcc wget apache2-dev curl git libxml2-dev linux-headers-$(uname -r) -y

        # obtain the PVM guest IP address and gateway
        $ ip addr show eth0 | grep 'inet '   # this is the PVM_IP

        # edit the /etc/motd with configuration details

        # enable root login with ssh
        # edit /etc/ssh/sshd_config
        # change "PermitRootLogin prohibit-password" to "PermitRootLogin yes"
        $ systemctl restart sshd

        # change hostname in /etc/hostname and /etc/hosts to "xd3-pvm"
        $ shutdown -r now

        # drop the PVM guest console
        $ Ctrl-]

### Setup environment on Dom0
    $ ip route | grep default   # this is the _GW
    # add the following environment variables to $HOME/.bashrc
    export MWROOT='/home/gregp/Projects/GIT/magicwand-commsbackbone'
    export PVM_USER='pvm'
    export INS_DIR=$MWROOT'/ins/ins-rump/apps/ins-app'
    export RUMP_IP='192.168.1.100'   # Free IP address for Rump/INS
    export _GW='192.168.1.1'         # IP address of Rump/INS gateway
    export PVM_IP='192.168.1.101'    # IP address of PVM

    # logout and log back in

### Setup software build environment on Dom0
    $ cd $MWROOT/util

    # copy contents of Makefile.template into Makefile for each project directory
    $ ./mw_prep

    # destroy current INS
    $ sudo xl destroy mw-ins-rump

    # set up the /mw key root in the XenStore
    $ ./mw_setkeys

    # copy mwcomms source code to PVM
    $ ssh-copy-id $PVM_USER@$PVM_IP
    $ ./mw_copy_to_pvm

    # build rumprun (using shell defined function)
    $ cd $MWROOT/ins/ins-rump
    $ source RUMP_ENV.sh
    # both 'dbgbuildrump' and 'dbgrebuildrump' are available
    # 'dbgrebuildrump' attempts to cleanup previous builds
    $ dbgrebuildrump

### Setup PVM
    $ ssh $PVM_USER@$PVM_IP

    # install and configure apache v2.2.11
    $ sudo mkdir -p /opt/httpd/src ; cd /opt/httpd
    $ sudo wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
    $ sudo tar -zxvf httpd-2.2.11.tar.gz -C src
    $ cd src/httpd-2.2.11
    $ sudo ./configure --prefix=/opt/httpd/ --with-included-apr
    $ sudo make ; sudo make install

    # add ld preload directive to apache
    $ sudo vi /opt/httpd/bin/envvars
    export LD_PRELOAD=/home/pvm/protvm/user/wrapper/tcp_ip_wrapper.so

    # disable ipv6
    $ sudo vi /opt/httpd/conf/httpd.conf
    Listen 0.0.0.0:80
    Listen 0.0.0.0:443

    # set apache server hostname
    $ sudo vi /opt/httpd/conf/httpd.conf
    ServerName xd3-pvm

    # disable sendfile
    $ sudo vi /opt/httpd/conf/httpd.conf
    EnableSendfile off

    # setup Wordpress and MySQL
    $ mkdir -p $HOME/code ; cd $HOME/code
    $ curl -o wordpress.tar.gz -fSL "https://wordpress.org/wordpress-4.7.3.tar.gz"
    $ sudo tar -xzf wordpress.tar.gz -C /usr/src/
    $ sudo chown -R www-data:www-data /usr/src/wordpress
    $ cd $HOME
    $ git clone git@github.com:twosixlabs/magicwand-appinstrumentation.git
    $ cp magicwand-appinstrumentation/suts/apache-wp/config/install-mysql.sh $HOME/code/
    $ cp magicwand-appinstrumentation/suts/apache-wp/content/mw-wordpress.sql $HOME/code/
    $ cp magicwand-appinstrumentation/suts/xray-apache-wp/config/slipstream-hostname-xray.py $HOME/code/
    $ sudo cp magicwand-appinstrumentation/suts/apache-wp/config/wp-config.php /opt/httpd/htdocs/
    $ cd $HOME/code
    $ sed -i "s/localhost/`hostname`/g" mw-wordpress.sql
    $ sudo sed -i "s/{{WORDPRESS_HOSTNAME}}/`hostname`/g" /opt/httpd/htdocs/wp-config.php
    $ sudo ./install-mysql.sh
    $ sudo rm /opt/httpd/htdocs/index.htm*
    $ sudo cp -R /usr/src/wordpress/* /opt/httpd/htdocs

    # setup PHP
    $ wget https://php.net/get/php-5.5.38.tar.gz/from/this/mirror -O php-5.5.38.tar.gz
    $ tar -xvf php-5.5.38.tar.gz
    $ cd php-5.5.38
    $ sudo ./configure --with-apxs2=/opt/httpd/bin/apxs --with-mysql
    $ sudo make ; sudo make install
    $ sudo sed -i '1i<FilesMatch "\.ph(p[2-6]?|tml)$">\n    SetHandler application/x-httpd-php\n</FilesMatch>' /opt/httpd/conf/httpd.conf
    $ sudo sed -i 's/DirectoryIndex index.html/DirectoryIndex index.php/g' /opt/httpd/conf/httpd.conf
    $ echo "<?php phpinfo(); ?>" | sudo tee /opt/httpd/htdocs/info.php
    $ sudo cp php.ini-development /usr/local/lib/php.ini

    # build shim
    $ cd ~/protvm/user/wrapper
    $ make clean ; make

    # build and load mwcomms driver on PVM
    $ cd ~/protvm/kernel/mwcomms
    $ make clean ; make

    # load driver
    $ sudo ./load.sh

### Build and start INS on Dom0
    # ensure any previous INS instance has been destroyed
    # and XenStore is initialized and old keys are cleared
    $ cd $INS_DIR
    $ make clean ; make
    $ ./run.sh

### Start Apache on PVM
    $ sudo mkdir -p /var/log/output
    $ sudo /opt/httpd/bin/apachectl start

### Test setup from remote system
    # navigate to INS IP, should see default Wordpress page
    $ firefox --new-tab xd3-pvm