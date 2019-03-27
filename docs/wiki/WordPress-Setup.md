# Set up WordPress with Apache

### Use the following WordPress tutorials
#### [initial-server-setup-with-ubuntu-16-04](https://www.digitalocean.com/community/tutorials/initial-server-setup-with-ubuntu-16-04)
#### [how-to-install-linux-apache-mysql-php-lamp-stack-on-ubuntu-16-04](https://www.digitalocean.com/community/tutorials/how-to-install-linux-apache-mysql-php-lamp-stack-on-ubuntu-16-04)
#### [how-to-install-wordpress-with-lamp-on-ubuntu-16-04](https://www.digitalocean.com/community/tutorials/how-to-install-wordpress-with-lamp-on-ubuntu-16-04)

    # NOTE: Do NOT install or setup ufw firewall
    # NOTE: Do NOT setup SQL Security steps or install "validate password plugin"
    # NOTE: Do NOT install any optional PHP packages
    # NOTE: Do NOT install or setup TLS/SSl

### Set hostname in WordPress Database on PVM
During WordPress setup set the "WordPress Address (URL)" and "Site Address (URL)" to the PVM hostname.

#### set hostname using WordPress admin GUI
    # log into WordPress admin GUI
    # navigate to "Settings" then to "General"
    # set "WordPress Address (URL) value to "http://<pvm hostname>
    # set "Site Address (URL) value to "http://<pvm hostname>
    # click on "Save Change" at the bottom of the page

#### set hostname using mysql on PVM
    $ mysql -u root -p
    mysql> USE wordpress;
    mysql> SELECT * from wp_options where option_name IN ("siteurl", "home");

    # check option_value for each field, should be "http://xd3-pvm" (hostname of PVM)

    # set values if not correct
    mysql> UPDATE wp_options SET option_value = 'http://xd3-pvm' WHERE option_name = 'home';
    mysql> UPDATE wp_options SET option_value = 'http://xd3-pvm' WHERE option_name = 'siteurl';
    mysql> SELECT * from wp_options where option_name IN ("siteurl", "home");
    mysql> FLUSH TABLES wp_options;
    mysql> quit
    
