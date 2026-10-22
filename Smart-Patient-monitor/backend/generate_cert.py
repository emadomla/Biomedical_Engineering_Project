"""
Local SSL Certificate Generator for FastAPI / Uvicorn HTTPS testing.
Generates 'cert.pem' and 'key.pem' for localhost and LAN IP addresses.
"""
import ipaddress
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

def generate_self_signed_cert(cert_file="cert.pem", key_file="key.pem"):
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
    except ImportError:
        print("[ERROR] 'cryptography' library is not installed.")
        print("Please install it by running: pip install cryptography")
        sys.exit(1)

    print("[SSL] Generating 2048-bit RSA private key...")
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )

    print("[SSL] Configuring Subject Alternative Names (localhost, 127.0.0.1, 0.0.0.0)...")
    san_list = [
        x509.DNSName("localhost"),
        x509.IPAddress(ipaddress.IPv4Address("127.0.0.1")),
        x509.IPAddress(ipaddress.IPv4Address("0.0.0.0")),
    ]

    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "BioMonitor Biomedical"),
        x509.NameAttribute(NameOID.COMMON_NAME, "localhost"),
    ])

    now = datetime.now(timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(private_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=1))
        .not_valid_after(now + timedelta(days=365))  # 1 year validity
        .add_extension(
            x509.SubjectAlternativeName(san_list),
            critical=False,
        )
        .sign(private_key, hashes.SHA256())
    )

    output_dir = Path(__file__).resolve().parent
    key_path = output_dir / key_file
    cert_path = output_dir / cert_file

    with open(key_path, "wb") as f:
        f.write(
            private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption(),
            )
        )

    with open(cert_path, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))

    print(f"\n[SUCCESS] Generated SSL Certificate & Key:")
    print(f"  Certificate: {cert_path}")
    print(f"  Private Key: {key_path}")
    print("\nTo start Uvicorn with HTTPS, run:")
    print("  uvicorn app.main:app --host 0.0.0.0 --port 8000 --ssl-keyfile key.pem --ssl-certfile cert.pem\n")

if __name__ == "__main__":
    generate_self_signed_cert()
