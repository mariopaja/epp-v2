FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Fixes for PV devices of dom0less domains:
# - init-dom0less: domains with more than one vCPU got no xenstore
# - libxl: xl block-attach / network-attach aborted on dom0less domains
SRC_URI += " \
    file://0001-tools-init-dom0less-fix-vCPU-availability-nodes.patch \
    file://0002-tools-libxl-treat-an-unset-b_info.tpm-as-no-TPM.patch \
"
