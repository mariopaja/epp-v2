do_install:append() {
    sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' ${D}${sysconfdir}/ssh/sshd_config
    sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication yes/' ${D}${sysconfdir}/ssh/sshd_config
}
