#!/usr/bin/env python3
import argparse
import json
import os
import sys
from typing import Any, Optional

DUAL_BANK_CTR_MASK = 0x5A3C0000

# SHA algorithm used for image hashing per signing key type.
# "auto" lets the signing tool pick the default for the scheme (LMS/XMSS),
# while the remaining schemes require an explicit digest matching the key.
SHA_BY_KEY_TYPE = {
    "LMS_SHA256_M32_H10": "auto",
    "XMSS_SHA2_10_256":   "auto",
    "ML-DSA-44":          "sha256",
    "ML-DSA-65":          "sha256",
    "ML-DSA-87":          "sha256",
    "ECDSA-256":          "sha256",
    "ECDSA-384":          "sha384",
    "ECDSA-521":          "sha512",
}

ALLOWED_KEY_TYPES = tuple(SHA_BY_KEY_TYPE.keys())

# Image signature encoding expected by the bootloader per signing key type.
# The stateful-hash (LMS/XMSS) and ML-DSA verifiers consume the raw signature,
# whereas the ECDSA verifier (mbedTLS) expects an ASN.1/DER encoded signature.
SIG_ENCODING_BY_KEY_TYPE = {
    "LMS_SHA256_M32_H10": "raw",
    "XMSS_SHA2_10_256":   "raw",
    "ML-DSA-44":          "raw",
    "ML-DSA-65":          "raw",
    "ML-DSA-87":          "raw",
    "ECDSA-256":          "asn1",
    "ECDSA-384":          "asn1",
    "ECDSA-521":          "asn1",
}

def get_var(name: str, cli_value: Optional[str]) -> str:
    """
    Resolve a variable from CLI first, then environment.
    Raises if not provided by either source.
    """
    if cli_value is not None and cli_value.strip() != "":
        return cli_value.strip()
    env_val = os.environ.get(name)
    if env_val is not None and env_val.strip() != "":
        return env_val.strip()
    raise KeyError(f"{name} not provided (use --{name.lower()} or set environment variable {name})")

def replace_placeholder_in_json(obj: Any, placeholder: str, replacement: str) -> Any:
    """
    Recursively replace occurrences of placeholder in all string values within a JSON-like object.
    """
    if isinstance(obj, dict):
        return {k: replace_placeholder_in_json(v, placeholder, replacement) for k, v in obj.items()}
    if isinstance(obj, list):
        return [replace_placeholder_in_json(v, placeholder, replacement) for v in obj]
    if isinstance(obj, str):
        return obj.replace(placeholder, replacement)
    return obj

def main():
    parser = argparse.ArgumentParser(
        description="Generate JSON from a template by replacing 'MAJ.MIN.REV+BN' using CLI/env variables."
    )
    parser.add_argument("--maj", help="Major version (or set env MAJ)")
    parser.add_argument("--min", help="Minor version (or set env MIN)")
    parser.add_argument("--rev", help="Revision (or set env REV)")
    parser.add_argument("--bn",  help="Build number (or set env BN)")
    parser.add_argument("--key",      help="Image signing key path (or set env KEY)")
    parser.add_argument("--key-type", help="Image signing key type (or set env KEY_TYPE). "
                        f"Allowed values: {', '.join(ALLOWED_KEY_TYPES)}")
    parser.add_argument("--enc-key",  help="Image encryption key path (or set env ENC_KEY). "
                        "Used only when image encryption is enabled; may be empty otherwise.")
    parser.add_argument("--sha", choices=("sha256", "sha384", "sha512"),
                        help="Override image hash / HashML-DSA pre-hash (must match the bootloader build).")
    parser.add_argument("--template", "-t", required=True, help="Path to the template JSON file.")
    parser.add_argument("--output", "-o", required=True, help="Path to write the generated JSON file.")
    parser.add_argument("--placeholder", "-p", default="MAJ.MIN.REV+BN",
                        help="Placeholder string to replace (default: MAJ.MIN.REV+BN).")
    parser.add_argument("--indent", type=int, default=2, help="Indent level for output JSON (default: 2).")
    args = parser.parse_args()

    try:
        maj = get_var("MAJ", args.maj)
        min_ = get_var("MIN", args.min)
        rev = get_var("REV", args.rev)
        bn = str(int(get_var("BN", args.bn)) | DUAL_BANK_CTR_MASK)
        key = get_var("KEY", args.key)
        key_type = get_var("KEY_TYPE", args.key_type)
    except KeyError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Encryption key is optional: only consumed by the encrypted update-image
    # templates. Default to an empty string when encryption is disabled.
    enc_key = (args.enc_key or os.environ.get("ENC_KEY") or "").strip()

    if key_type not in ALLOWED_KEY_TYPES:
        print(f"Error: KEY_TYPE '{key_type}' is not valid. Allowed values: {', '.join(ALLOWED_KEY_TYPES)}",
              file=sys.stderr)
        sys.exit(1)

    sha = args.sha or SHA_BY_KEY_TYPE[key_type]
    sig_encoding = SIG_ENCODING_BY_KEY_TYPE[key_type]

    version_str = f"{maj}.{min_}.{rev}+{bn}"

    try:
        with open(args.template, "r", encoding="utf-8") as f:
            template_obj = json.load(f)
    except Exception as e:
        print(f"Error reading template JSON: {e}", file=sys.stderr)
        sys.exit(1)

    result_obj = replace_placeholder_in_json(template_obj, "MAJ.MIN.REV+BN", version_str)
    result_obj = replace_placeholder_in_json(result_obj, "KEY_PATH", key)
    result_obj = replace_placeholder_in_json(result_obj, "KEY_TYPE_VALUE", key_type)
    result_obj = replace_placeholder_in_json(result_obj, "SHA_VALUE", sha)
    result_obj = replace_placeholder_in_json(result_obj, "SIG_ENCODING_VALUE", sig_encoding)
    result_obj = replace_placeholder_in_json(result_obj, "ENCKEY_FILE", enc_key)

    try:
        out_dir = os.path.dirname(args.output)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump(result_obj, f, indent=args.indent, ensure_ascii=False)
            f.write("\n")
    except Exception as e:
        print(f"Error writing output JSON: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()