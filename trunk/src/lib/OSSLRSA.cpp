/* $Id$ */

/*
 * Copyright (c) 2010 SURFnet bv
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************
 OSSLRSA.cpp

 OpenSSL RSA asymmetric algorithm implementation
 *****************************************************************************/

#include "config.h"
#include "log.h"
#include "OSSLRSA.h"
#include "CryptoFactory.h"
#include "RSAParameters.h"
#include "OSSLRSAKeyPair.h"
#include <algorithm>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

// Constructor
OSSLRSA::OSSLRSA()
{
	pCurrentHash = NULL;
	pSecondHash = NULL;
}

// Destructor
OSSLRSA::~OSSLRSA()
{
	if (pCurrentHash != NULL)
	{
		delete pCurrentHash;
	}
	
	if (pSecondHash != NULL)
	{
		delete pSecondHash;
	}
}
	
// Signing functions
bool OSSLRSA::signInit(PrivateKey* privateKey, const std::string mechanism)
{
	if (!AsymmetricAlgorithm::signInit(privateKey, mechanism))
	{
		return false;
	}

	// Check if the private key is the right type
	if (!privateKey->isOfType(OSSLRSAPrivateKey::type))
	{
		ERROR_MSG("Invalid key type supplied");

		return false;
	}

	std::string lowerMechanism;
	lowerMechanism.resize(mechanism.size());
	std::transform(mechanism.begin(), mechanism.end(), lowerMechanism.begin(), tolower);

	if (!lowerMechanism.compare("rsa-md5-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("md5");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha1-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha1");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha256-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha256");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha512-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha512");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-ssl"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("md5");
		pSecondHash = CryptoFactory::i()->getHashAlgorithm("sha1");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}

		if (!pSecondHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
			
			delete pSecondHash;
			pSecondHash = NULL;
		}
	}

	if (pCurrentHash == NULL)
	{
		ByteString dummy;
		AsymmetricAlgorithm::signFinal(dummy);

		return false;
	}

	return true;
}

bool OSSLRSA::signUpdate(const ByteString& dataToSign)
{
	if (!AsymmetricAlgorithm::signUpdate(dataToSign))
	{
		return false;
	}

	if (!pCurrentHash->hashUpdate(dataToSign))
	{
		delete pCurrentHash;
		pCurrentHash = NULL;

		ByteString dummy;
		AsymmetricAlgorithm::signFinal(dummy);

		return false;
	}

	if ((pSecondHash != NULL) && !pSecondHash->hashUpdate(dataToSign))
	{
		delete pCurrentHash;
		pCurrentHash = NULL;

		delete pSecondHash;
		pSecondHash = NULL;

		ByteString dummy;
		AsymmetricAlgorithm::signFinal(dummy);

		return false;
	}

	return true;
}

bool OSSLRSA::signFinal(ByteString& signature)
{	
	// Save necessary state before calling super class signFinal
	OSSLRSAPrivateKey* pk = (OSSLRSAPrivateKey*) currentPrivateKey;

	std::string lowerMechanism;
	lowerMechanism.resize(currentMechanism.size());
	std::transform(currentMechanism.begin(), currentMechanism.end(), lowerMechanism.begin(), tolower);

	if (!AsymmetricAlgorithm::signFinal(signature))
	{
		return false;
	}

	ByteString firstHash, secondHash;

	bool bFirstResult = pCurrentHash->hashFinal(firstHash);
	bool bSecondResult = (pSecondHash != NULL) ? pSecondHash->hashFinal(secondHash) : true;

	delete pCurrentHash;
	pCurrentHash = NULL;

	if (pSecondHash != NULL)
	{
		delete pSecondHash;

		pSecondHash = NULL;
	}

	if (!bFirstResult || !bSecondResult)
	{
		return false;
	}
	
	ByteString digest = firstHash + secondHash;

	// Resize the data block for the signature to the modulus size of the key
	signature.resize(pk->getN().size());

	// Determine the signature NID type
	int type = 0;

	if (!lowerMechanism.compare("rsa-md5-pkcs"))
	{
		type = NID_md5;
	}
	else if (!lowerMechanism.compare("rsa-sha1-pkcs"))
	{
		type = NID_sha1;
	}
	else if (!lowerMechanism.compare("rsa-sha256-pkcs"))
	{
		type = NID_sha256;
	}
	else if (!lowerMechanism.compare("rsa-sha512-pkcs"))
	{
		type = NID_sha512;
	}
	else if (!lowerMechanism.compare("rsa-ssl"))
	{
		type = NID_md5_sha1;
	}

	// Perform the signature operation
	unsigned int sigLen = signature.size();

	RSA* rsa = pk->getOSSLKey();

	if (!RSA_blinding_on(rsa, NULL))
	{
		ERROR_MSG("Failed to turn blinding on for OpenSSL RSA key");
	
		return false;
	}

	bool rv = (RSA_sign(type, &digest[0], digest.size(), &signature[0], &sigLen, pk->getOSSLKey()) == 1);

	RSA_blinding_off(rsa);

	signature.resize(sigLen);

	return rv;
}

// Verification functions
bool OSSLRSA::verifyInit(PublicKey* publicKey, const std::string mechanism)
{
	if (!AsymmetricAlgorithm::verifyInit(publicKey, mechanism))
	{
		return false;
	}

	// Check if the private key is the right type
	if (!publicKey->isOfType(OSSLRSAPublicKey::type))
	{
		ERROR_MSG("Invalid key type supplied");

		return false;
	}

	std::string lowerMechanism;
	lowerMechanism.resize(mechanism.size());
	std::transform(mechanism.begin(), mechanism.end(), lowerMechanism.begin(), tolower);

	if (!lowerMechanism.compare("rsa-md5-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("md5");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha1-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha1");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha256-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha256");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-sha512-pkcs"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("sha512");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}
	}
	else if (!lowerMechanism.compare("rsa-ssl"))
	{
		pCurrentHash = CryptoFactory::i()->getHashAlgorithm("md5");
		pSecondHash = CryptoFactory::i()->getHashAlgorithm("sha1");

		if (!pCurrentHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
		}

		if (!pSecondHash->hashInit())
		{
			delete pCurrentHash;
			pCurrentHash = NULL;
			
			delete pSecondHash;
			pSecondHash = NULL;
		}
	}

	if (pCurrentHash == NULL)
	{
		ByteString dummy;
		AsymmetricAlgorithm::verifyFinal(dummy);

		return false;
	}

	return true;
}

bool OSSLRSA::verifyUpdate(const ByteString& originalData)
{
	if (!AsymmetricAlgorithm::verifyUpdate(originalData))
	{
		return false;
	}

	if (!pCurrentHash->hashUpdate(originalData))
	{
		delete pCurrentHash;
		pCurrentHash = NULL;

		ByteString dummy;
		AsymmetricAlgorithm::verifyFinal(dummy);

		return false;
	}

	if ((pSecondHash != NULL) && !pSecondHash->hashUpdate(originalData))
	{
		delete pCurrentHash;
		pCurrentHash = NULL;

		delete pSecondHash;
		pSecondHash = NULL;

		ByteString dummy;
		AsymmetricAlgorithm::verifyFinal(dummy);

		return false;
	}

	return true;
}

bool OSSLRSA::verifyFinal(const ByteString& signature)
{
	// Save necessary state before calling super class verifyFinal
	OSSLRSAPublicKey* pk = (OSSLRSAPublicKey*) currentPublicKey;

	std::string lowerMechanism;
	lowerMechanism.resize(currentMechanism.size());
	std::transform(currentMechanism.begin(), currentMechanism.end(), lowerMechanism.begin(), tolower);

	if (!AsymmetricAlgorithm::verifyFinal(signature))
	{
		return false;
	}

	ByteString firstHash, secondHash;

	bool bFirstResult = pCurrentHash->hashFinal(firstHash);
	bool bSecondResult = (pSecondHash != NULL) ? pSecondHash->hashFinal(secondHash) : true;

	delete pCurrentHash;
	pCurrentHash = NULL;

	if (pSecondHash != NULL)
	{
		delete pSecondHash;

		pSecondHash = NULL;
	}

	if (!bFirstResult || !bSecondResult)
	{
		return false;
	}
	
	ByteString digest = firstHash + secondHash;

	// Determine the signature NID type
	int type = 0;

	if (!lowerMechanism.compare("rsa-md5-pkcs"))
	{
		type = NID_md5;
	}
	else if (!lowerMechanism.compare("rsa-sha1-pkcs"))
	{
		type = NID_sha1;
	}
	else if (!lowerMechanism.compare("rsa-sha256-pkcs"))
	{
		type = NID_sha256;
	}
	else if (!lowerMechanism.compare("rsa-sha512-pkcs"))
	{
		type = NID_sha512;
	}
	else if (!lowerMechanism.compare("rsa-ssl"))
	{
		type = NID_md5_sha1;
	}

	// Perform the verify operation
	bool rv = (RSA_verify(type, &digest[0], digest.size(), (unsigned char*) signature.const_byte_str(), signature.size(), pk->getOSSLKey()) == 1);

	if (!rv) ERROR_MSG("RSA verify failed (%d, %s in %s in %s)", 
			    ERR_get_error(),
		  	    ERR_reason_error_string(ERR_get_error()),
			    ERR_func_error_string(ERR_get_error()),
			    ERR_lib_error_string(ERR_get_error()));

	return rv;
}

// Encryption functions
bool OSSLRSA::encrypt(PublicKey* publicKey, const ByteString& data, ByteString& encryptedData, const std::string padding)
{
	// Check if the public key is the right type
	if (!publicKey->isOfType(OSSLRSAPublicKey::type))
	{
		ERROR_MSG("Invalid key type supplied");

		return false;
	}

	std::string lowerPadding;
	lowerPadding.resize(padding.size());
	std::transform(padding.begin(), padding.end(), lowerPadding.begin(), tolower);

	// Retrieve the OpenSSL key object
	RSA* rsa = ((OSSLRSAPublicKey*) publicKey)->getOSSLKey();

	// Check the data and padding algorithm
	int osslPadding = 0;

	if (!lowerPadding.compare("rsa-pkcs1"))
	{
		// The size of the input data cannot be more than the modulus
		// length of the key - 11
		if (data.size() > (size_t) (RSA_size(rsa) - 11))
		{
			ERROR_MSG("Too much data supplied for RSA PKCS #1 encryption");

			return false;
		}

		osslPadding = RSA_PKCS1_PADDING;
	}
	else if (!lowerPadding.compare("rsa-pkcs1-oaep"))
	{
		// The size of the input data cannot be more than the modulus
		// length of the key - 41
		if (data.size() > (size_t) (RSA_size(rsa) - 41))
		{
			ERROR_MSG("Too much data supplied for RSA OAEP encryption");

			return false;
		}

		osslPadding = RSA_PKCS1_OAEP_PADDING;
	}
	else if (!lowerPadding.compare("rsa-sslv23"))
	{
		// FIXME: There is no known length check
		osslPadding = RSA_SSLV23_PADDING;
	}
	else if (!lowerPadding.compare("raw"))
	{
		// The size of the input data should be exactly equal to the modulus length
		if (data.size() != (size_t) RSA_size(rsa))
		{
			ERROR_MSG("Incorrect amount of input data supplied for raw RSA encryption");

			return false;
		}

		osslPadding = RSA_NO_PADDING;
	}
	else
	{
		ERROR_MSG("Invalid padding mechanism supplied (%s)", padding.c_str());

		return false;
	}

	// Perform the RSA operation
	encryptedData.resize(RSA_size(rsa));

	if (RSA_public_encrypt(data.size(), (unsigned char*) data.const_byte_str(), &encryptedData[0], rsa, osslPadding) == -1)
	{
		ERROR_MSG("RSA public key encryption failed: %s in %s in %s",
			   ERR_reason_error_string(ERR_get_error()),
			   ERR_func_error_string(ERR_get_error()),
			   ERR_lib_error_string(ERR_get_error()));

		return false;
	}

	return true;
}

// Decryption functions
bool OSSLRSA::decrypt(PrivateKey* privateKey, const ByteString& encryptedData, ByteString& data, const std::string padding)
{
	// Check if the private key is the right type
	if (!privateKey->isOfType(OSSLRSAPrivateKey::type))
	{
		ERROR_MSG("Invalid key type supplied");

		return false;
	}

	// Retrieve the OpenSSL key object
	RSA* rsa = ((OSSLRSAPrivateKey*) privateKey)->getOSSLKey();

	// Check the input size
	if (encryptedData.size() != (size_t) RSA_size(rsa))
	{
		ERROR_MSG("Invalid amount of input data supplied for RSA decryption");

		return false;
	}

	std::string lowerPadding;
	lowerPadding.resize(padding.size());
	std::transform(padding.begin(), padding.end(), lowerPadding.begin(), tolower);

	// Determine the OpenSSL padding algorithm
	int osslPadding = 0;

	if (!lowerPadding.compare("rsa-pkcs1"))
	{
		osslPadding = RSA_PKCS1_PADDING;
	}
	else if (!lowerPadding.compare("rsa-pkcs1-oaep"))
	{
		osslPadding = RSA_PKCS1_OAEP_PADDING;
	}
	else if (!lowerPadding.compare("rsa-sslv23"))
	{
		osslPadding = RSA_SSLV23_PADDING;
	}
	else if (!lowerPadding.compare("raw"))
	{
		osslPadding = RSA_NO_PADDING;
	}
	else
	{
		ERROR_MSG("Invalid padding mechanism supplied (%s)", padding.c_str());

		return false;
	}

	// Perform the RSA operation
	data.resize(RSA_size(rsa));

	int decSize = RSA_private_decrypt(encryptedData.size(), (unsigned char*) encryptedData.const_byte_str(), &data[0], rsa, osslPadding);

	if (decSize == -1)
	{
		ERROR_MSG("RSA private key decryption failed: %s in %s in %s",
			   ERR_reason_error_string(ERR_get_error()),
			   ERR_func_error_string(ERR_get_error()),
			   ERR_lib_error_string(ERR_get_error()));

		return false;
	}

	data.resize(decSize);

	return true;
}

// Key factory
bool OSSLRSA::generateKeyPair(AsymmetricKeyPair** ppKeyPair, size_t keySize, void* parameters /* = NULL */, RNG* rng /* = NULL */)
{
	// Check parameters
	if ((ppKeyPair == NULL) ||
	    (keySize < 512) ||
	    (keySize > 16384) ||
	    (parameters == NULL))
	{
		return false;
	}

	if (keySize < 1024)
	{
		WARNING_MSG("Using an RSA key size < 1024 bits is not recommended");
	}

	RSAParameters* params = (RSAParameters*) parameters;

	try
	{
		if (params->magic != RSA_PARAMETER_MAGIC)
		{
			return false;
		}
	}
	catch (...)
	{
		return false;
	}

	// Retrieve the desired public exponent
	unsigned long e = params->e.long_val();

	// Check the public exponent
	if ((e == 0) || (e % 2 != 1))
	{
		ERROR_MSG("Invalid RSA public exponent %d", e);

		return false;
	}

	// Generate the key-pair
	RSA* rsa = RSA_generate_key(keySize, e, NULL, NULL);

	// Check if the key was successfully generated
	if (rsa == NULL)
	{
		ERROR_MSG("RSA key generation failed: %s in %s in %s",
			  ERR_reason_error_string(ERR_get_error()),
			  ERR_func_error_string(ERR_get_error()),
			  ERR_lib_error_string(ERR_get_error()));

		return false;
	}

	// Create an asymmetric key-pair object to return
	OSSLRSAKeyPair* kp = new OSSLRSAKeyPair();

	((OSSLRSAPublicKey*) kp->getPublicKey())->setFromOSSL(rsa);
	((OSSLRSAPrivateKey*) kp->getPrivateKey())->setFromOSSL(rsa);

	*ppKeyPair = kp;

	// Release the key
	RSA_free(rsa);

	return true;
}

bool OSSLRSA::reconstructKeyPair(AsymmetricKeyPair** ppKeyPair, ByteString& serialisedData)
{
	// Check input
	if ((ppKeyPair == NULL) ||
	    (serialisedData.size() == 0))
	{
		return false;
	}

	ByteString dPub = ByteString::chainDeserialise(serialisedData);
	ByteString dPriv = ByteString::chainDeserialise(serialisedData);

	OSSLRSAKeyPair* kp = new OSSLRSAKeyPair();

	bool rv = true;

	if (!((RSAPublicKey*) kp->getPublicKey())->deserialise(dPub))
	{
		rv = false;
	}

	if (!((RSAPrivateKey*) kp->getPrivateKey())->deserialise(dPriv))
	{
		rv = false;
	}

	if (!rv)
	{
		delete kp;

		return false;
	}

	*ppKeyPair = kp;

	return true;
}

bool OSSLRSA::reconstructPublicKey(PublicKey** ppPublicKey, ByteString& serialisedData)
{
	// Check input
	if ((ppPublicKey == NULL) ||
	    (serialisedData.size() == 0))
	{
		return false;
	}

	OSSLRSAPublicKey* pub = new OSSLRSAPublicKey();

	if (!pub->deserialise(serialisedData))
	{
		delete pub;

		return false;
	}

	*ppPublicKey = pub;

	return true;
}

bool OSSLRSA::reconstructPrivateKey(PrivateKey** ppPrivateKey, ByteString& serialisedData)
{
	// Check input
	if ((ppPrivateKey == NULL) ||
	    (serialisedData.size() == 0))
	{
		return false;
	}

	OSSLRSAPrivateKey* priv = new OSSLRSAPrivateKey();

	if (!priv->deserialise(serialisedData))
	{
		delete priv;

		return false;
	}

	*ppPrivateKey = priv;

	return true;
}

PublicKey* OSSLRSA::newPublicKey()
{
	return (PublicKey*) new OSSLRSAPublicKey();
}

PrivateKey* OSSLRSA::newPrivateKey()
{
	return (PrivateKey*) new OSSLRSAPrivateKey();
}
