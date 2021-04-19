#
# SSL Certificate creation in the test environment
#
import subprocess
import os, os.path, time, shutil


class SSLCert:
    """
    Calls openssl to generate SSL certificates and sign them.
    """

    def __init__(self, directory, basename, CN):
        self.directory = directory
        self.basename = basename
        self.CN = CN

        self.csr = None
        self.key = None
        self.crt = None

        self.rootKey = None
        self.rootCert = None
        self.crl = None

        self.sudo_mkdir_p()

    def sudo_mkdir_p(self):
        if os.path.isdir(self.directory):
            return True

        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "mkdir",
                "-p",
                self.directory,
            ]
        )
        assert p.wait() == 0

    def create_root_cert(self):
        # avoid bugs where we overwrite certificates in a given directory
        assert self.csr is None
        assert self.crt is None
        assert self.key is None

        self.csr = os.path.join(self.directory, "%s.csr" % self.basename)
        self.key = os.path.join(self.directory, "%s.key" % self.basename)
        self.crt = os.path.join(self.directory, "%s.crt" % self.basename)

        # first create a certificate signing request (CSR) and a public/private
        # key file
        print()
        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "openssl",
                "req",
                "-new",
                "-nodes",
                "-text",
                "-out",
                self.csr,
                "-keyout",
                self.key,
                "-subj",
                self.CN,
            ]
        )
        assert p.wait() == 0

        p = subprocess.Popen(["chmod", "og-rwx", self.key])
        assert p.wait() == 0

        # Then, sign the request with the key to create a root certificate
        # authority
        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "openssl",
                "x509",
                "-req",
                "-in",
                self.csr,
                "-text",
                "-days",
                "3650",
                "-extfile",
                "/etc/ssl/openssl.cnf",
                "-extensions",
                "v3_ca",
                "-signkey",
                self.key,
                "-out",
                self.crt,
            ]
        )
        assert p.wait() == 0

    def create_signed_certificate(self, rootSSLCert):
        # avoid bugs where we overwrite certificates in a given directory
        assert self.csr is None
        assert self.crt is None
        assert self.key is None

        self.rootKey = rootSSLCert.key
        self.rootCert = rootSSLCert.crt
        self.crl = rootSSLCert.crl

        self.crt = os.path.join(self.directory, "%s.crt" % self.basename)
        self.csr = os.path.join(self.directory, "%s.csr" % self.basename)
        self.key = os.path.join(self.directory, "%s.key" % self.basename)

        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "openssl",
                "req",
                "-new",
                "-nodes",
                "-text",
                "-out",
                self.csr,
                "-keyout",
                self.key,
                "-subj",
                self.CN,
            ]
        )
        assert p.wait() == 0

        p = subprocess.Popen(["chmod", "og-rwx", self.key])
        assert p.wait() == 0

        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "openssl",
                "x509",
                "-req",
                "-in",
                self.csr,
                "-text",
                "-days",
                "365",
                "-CA",
                self.rootCert,
                "-CAkey",
                self.rootKey,
                "-CAcreateserial",
                "-out",
                self.crt,
            ]
        )
        assert p.wait() == 0

        print("openssl verify -CAfile %s %s" % (self.rootCert, self.crt))
        p = subprocess.Popen(
            [
                "sudo",
                "-E",
                "-u",
                os.getenv("USER"),
                "env",
                "PATH=" + os.getenv("PATH"),
                "openssl",
                "verify",
                "-show_chain",
                "-CAfile",
                self.rootCert,
                self.crt,
            ]
        )
        assert p.wait() == 0
