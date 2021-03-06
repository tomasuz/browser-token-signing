/*
* Chrome Token Signing Native Host
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "PKCS11CertificateSelector.h"
#include "PKCS11CardManager.h"
#include "BinaryUtils.h"
#include "HostExceptions.h"
extern "C" {
#include "esteid_log.h"
}

using namespace std;

void PKCS11CertificateSelector::initialize() {
	if (hMemoryStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, NULL, 0, NULL)) {
		EstEID_log("Opened a memory store.");
	} else {	
		EstEID_log("Error opening a memory store.");
		throw TechnicalException("Error opening a memory store.");
	}
	fetchAllSigningCertificates();
}

void PKCS11CertificateSelector::fetchAllSigningCertificates() {
	try {
		unique_ptr<PKCS11CardManager> manager;
		bool certificateAddedToMemoryStore = false;
		vector<CK_SLOT_ID> foundTokens = createCardManager()->getAvailableTokens();
		for (auto &token : foundTokens) {
			try {
				manager.reset(createCardManager()->getManagerForReader(token));
			}
			catch (PKCS11TokenNotRecognized &ex) {
				EstEID_log("%s", ex.what());
				continue;
			}
			catch (PKCS11TokenNotPresent &ex) {
				EstEID_log("%s", ex.what());
				continue;
			}
			if (manager->hasSignCert()) {
				addCertificateToMemoryStore(manager->getSignCert());
				certificateAddedToMemoryStore = true;
			}
		}
		if (foundTokens.size() > 0 && !certificateAddedToMemoryStore) {
			throw PKCS11TokenNotRecognized();
		}
	}
	catch (const std::runtime_error &a) {
		EstEID_log("Technical error: %s", a.what());
		throw TechnicalException("Error getting certificate manager: " + string(a.what()));
	}
}

PKCS11CardManager* PKCS11CertificateSelector::createCardManager() {
	if (driverPath.empty()) {
		return PKCS11CardManager::instance();
	}
	return PKCS11CardManager::instance(driverPath);
}

void PKCS11CertificateSelector::addCertificateToMemoryStore(std::vector<unsigned char> signCert) {
	PCCERT_CONTEXT cert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, signCert.data(), signCert.size());
	if (CertAddCertificateContextToStore(hMemoryStore, cert, CERT_STORE_ADD_USE_EXISTING, NULL)) {
		EstEID_log("Certificate added to the memory store.");
	}
	else {
		EstEID_log("Could not add the certificate to the memory store.");
		throw TechnicalException("Could not add certificate to the memory store.");
	}
}

vector<unsigned char> PKCS11CertificateSelector::getCert() {
	CRYPTUI_SELECTCERTIFICATE_STRUCT pcsc = { sizeof(pcsc) };
	pcsc.pFilterCallback = nullptr; //already filtered in PKCS11CardManager
	pcsc.pvCallbackData = nullptr;
	pcsc.cDisplayStores = 1;
	pcsc.rghDisplayStores = &hMemoryStore;
	PCCERT_CONTEXT cert_context = CryptUIDlgSelectCertificate(&pcsc);

	if (!cert_context) {
		CertCloseStore(hMemoryStore, 0);
		throw UserCancelledException();
	}
	vector<unsigned char> certData(cert_context->pbCertEncoded, cert_context->pbCertEncoded + cert_context->cbCertEncoded);
	CertFreeCertificateContext(cert_context);
	CertCloseStore(hMemoryStore, 0);
	return certData;
}
