do_install:append() {
    if [ "${EPP_SSH_ROOT_LOGIN}" = "yes" ]; then
        sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' ${D}${sysconfdir}/ssh/sshd_config
        sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication yes/' ${D}${sysconfdir}/ssh/sshd_config
    fi
}
