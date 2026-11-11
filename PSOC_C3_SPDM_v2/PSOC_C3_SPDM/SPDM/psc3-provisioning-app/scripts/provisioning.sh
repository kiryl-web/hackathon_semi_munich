#! /bin/bash

edgeprotecttools -t psoc_c3 init
edgeprotecttools create-key --key-type ECDSA-P256 -o keys/oem_rot_priv_key_0.pem keys/oem_rot_pub_key_0.pem
edgeprotecttools create-key --key-type ECDSA-P256 -o keys/oem_rot_priv_key_1.pem keys/oem_rot_pub_key_1.pem
edgeprotecttools -t psoc_c3 provision-device -p ns_policy/policy_oem_provisioning.json