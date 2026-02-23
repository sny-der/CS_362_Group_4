import os
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# Will be given 1 mb chunks of data to encrypt and decrypt in an array.
# Also will be given file name, size, and length of chunk array. 

class handshake_manager:
    def __init__(self):
        self.private_key = ec.generate_private_key(ec.SECP256R1())
        self.public_key = self.private_key.public_key()

    def get_public_key_bytes(self) -> bytes:
        return self.public_key.public_bytes(
            encoding=serialization.Encoding.X962,
            format=serialization.PublicFormat.UncompressedPoint
        )
    
    def calculate_shared_key(self, peer_public_key_bytes: bytes) -> bytes:
        peer_public_key = ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), 
            peer_public_key_bytes
        )
        shared_key = self.private_key.exchange(ec.ECDH(), peer_public_key)
        return shared_key

class encryption_engine:

    def __init__(self, shared_key: bytes):
        # Derive a 32-byte key from the shared key using HKDF
        self.key = self._derive_key(shared_key)
        self.aesgcm = AESGCM(self.key)

    def _derive_key(self, shared_key: bytes) -> bytes:
        # HKDF to turn DH into a symmetric AES key
        hkdf = HKDF(
            algorithm=hashes.SHA256(),
            length=32,
            salt=None,
            info=b'p2p-file-transfer-session',
        ).derive(shared_key)
        return hkdf

    def encrypt_chunk_list(self, chunk_list: list[bytes]) -> list[bytes]:
        return [self.encrypt_chunk(chunk) for chunk in chunk_list]

    def encrypt_chunk(self, data: bytes) -> bytes:
        # Generate a random 12-byte nonce for AES-GCM
        nonce = os.urandom(12)
        ciphertext = self.aesgcm.encrypt(nonce, data, None)
        # Prepend the nonce to the ciphertext for later use in decryption
        return nonce + ciphertext
    
    def decrypt_chunk(self, encrypted_data: bytes) -> bytes:
        # Extract the nonce from the beginning of the encrypted data
        nonce = encrypted_data[:12]
        ciphertext = encrypted_data[12:]
        return self.aesgcm.decrypt(nonce, ciphertext, None)
    



#Main logic for testing
if __name__ == "__main__":
    # Simulate two peers performing a handshake and encrypting/decrypting data
    peer1 = handshake_manager()
    peer2 = handshake_manager()

    # Exchange public keys
    peer1_public_key = peer1.get_public_key_bytes()
    peer2_public_key = peer2.get_public_key_bytes()

    # Calculate shared keys
    shared_key1 = peer1.calculate_shared_key(peer2_public_key)
    shared_key2 = peer2.calculate_shared_key(peer1_public_key)

    # Initialize encryption engines with the shared keys
    engine1 = encryption_engine(shared_key1)
    engine2 = encryption_engine(shared_key2)

    # Sample data to encrypt
    data_chunks = [b"Hello, this is chunk 1.", b"This is chunk 2.", b"And this is chunk 3."]

    # Peer 1 encrypts the data chunks
    encrypted_chunks = engine1.encrypt_chunk_list(data_chunks)

    # Peer 2 decrypts the received encrypted chunks
    decrypted_chunks = [engine2.decrypt_chunk(chunk) for chunk in encrypted_chunks]

    # Verify that the decrypted chunks match the original data
    assert decrypted_chunks == data_chunks
    print("Encryption and decryption successful! Decrypted chunks match original data.")

