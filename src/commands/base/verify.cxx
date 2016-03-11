/**
 * Copyright (C) 2015 Virgil Security Inc.
 *
 * Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     (1) Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     (2) Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *     (3) Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <tclap/CmdLine.h>

#include <virgil/crypto/VirgilByteArray.h>
#include <virgil/crypto/VirgilStreamSigner.h>
#include <virgil/crypto/stream/VirgilStreamDataSource.h>

#include <virgil/sdk/models/CardModel.h>

#include <cli/version.h>
#include <cli/pair.h>
#include <cli/util.h>

namespace vcrypto = virgil::crypto;
namespace vsdk = virgil::sdk;
namespace vcli = virgil::cli;

#ifdef SPLIT_CLI
#define MAIN main
#else
#define MAIN verify_main
#endif

static void checkFormatRecipientArg(const std::pair<std::string, std::string>& pairRecipientArg);

int MAIN(int argc, char** argv) {
    try {
        std::string description = "Verify data and signature with given user's identifier"
                                  " or with its Virgil Card.\n";

        std::vector<std::string> examples;
        examples.push_back("Virgil verify -i plain.txt -s plain.txt.sign -r email:bob@gmail.com\n");

        std::string descriptionMessage = virgil::cli::getDescriptionMessage(description, examples);

        // Parse arguments.
        TCLAP::CmdLine cmd(descriptionMessage, ' ', virgil::cli_version());

        TCLAP::ValueArg<std::string> inArg("i", "in", "Data to be verified. If omitted, stdin is used.", false, "",
                                           "file");

        TCLAP::ValueArg<std::string> outArg(
            "o", "out", "Verification result: success | failure. If omitted, stdout is used.", false, "", "file");

        TCLAP::ValueArg<std::string> signArg("s", "sign", "Digest sign.", true, "", "file");

        TCLAP::ValueArg<bool> unconfirmedArg("u", "unconfirmed", "Search Cards with unconfirmed "
                                                                 "identity. False by default.",
                                             false, "", "");

        TCLAP::ValueArg<std::string> recipientArg(
            "r", "recipient", "Recipient defined in format:\n"
                              "[id|vcard|email|pub-key]:<value>\n"
                              "where:\n"
                              "\t* if id, then <value> - recipient's UUID associated with Virgil Card identifier;\n"
                              "\t* if vcard, then <value> - recipient's Virgil Card/Cards file\n\t  stored locally;\n"
                              "\t* if email, then <value> - recipient's email;\n"
                              "\t* if pub-key, then <value> - recipient's Public Key.\n",
            true, "", "arg");

        cmd.add(recipientArg);
        cmd.add(unconfirmedArg);
        cmd.add(signArg);
        cmd.add(outArg);
        cmd.add(inArg);
        cmd.parse(argc, argv);

        auto recipientFormat = vcli::parsePair(recipientArg.getValue());
        checkFormatRecipientArg(recipientFormat);

        // Prepare input
        std::istream* inStream;
        std::ifstream inFile;
        if (inArg.getValue().empty() || inArg.getValue() == "-") {
            inStream = &std::cin;
        } else {
            inFile.open(inArg.getValue(), std::ios::in | std::ios::binary);
            if (!inFile) {
                throw std::invalid_argument("cannot read file: " + inArg.getValue());
            }
            inStream = &inFile;
        }

        // Verify data
        vcrypto::stream::VirgilStreamDataSource dataSource(*inStream);

        // Read sign
        std::ifstream signFile(signArg.getValue(), std::ios::in | std::ios::binary);
        if (!signFile) {
            throw std::invalid_argument("cannot read file: " + signArg.getValue());
        }
        vcrypto::VirgilByteArray sign((std::istreambuf_iterator<char>(signFile)), std::istreambuf_iterator<char>());

        // Create signer
        vcrypto::VirgilStreamSigner signer;

        std::string type = recipientFormat.first;
        std::string value = recipientFormat.second;

        if (value == "pub-key") {
            std::string pathToPublicKeyFile = value;
            vcrypto::VirgilByteArray publicKey = vcli::readPublicKey(pathToPublicKeyFile);
            bool verified = signer.verify(dataSource, sign, publicKey);
            if (verified) {
                vcli::writeBytes(outArg.getValue(), "success");
                return EXIT_SUCCESS;
            } else {
                vcli::writeBytes(outArg.getValue(), "failure");
                return EXIT_FAILURE;
            }

        } else {
            std::vector<vsdk::models::CardModel> recipientCards =
                vcli::getRecipientCards(type, value, unconfirmedArg.getValue());

            std::vector<std::string> verifiedInfo;
            size_t countVerifiedFailure = 0;
            for (const auto& recipientCard : recipientCards) {
                bool verified = signer.verify(dataSource, sign, recipientCard.getPublicKey().getKey());
                std::string recipientCardId = recipientCard.getId();
                if (verified) {
                    verifiedInfo.push_back("card-id " + recipientCardId + " - success\n");
                } else {
                    ++countVerifiedFailure;
                    verifiedInfo.push_back("card-id " + recipientCardId + " - failure\n");
                }
            }

            std::string info;
            for (const auto& vi : verifiedInfo) {
                info += vi;
            }
            vcli::writeBytes(outArg.getValue(), info);

            if (countVerifiedFailure == recipientCards.size()) {
                return EXIT_FAILURE;
            }
        }

    } catch (TCLAP::ArgException& exception) {
        std::cerr << "verify. Error: " << exception.error() << " for arg " << exception.argId() << std::endl;
        return EXIT_FAILURE;
    } catch (std::exception& exception) {
        std::cerr << "verify. Error: " << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void checkFormatRecipientArg(const std::pair<std::string, std::string>& pairRecipientArg) {
    const std::string type = pairRecipientArg.first;
    if (type != "id" && type != "vcard" && type != "email" && type != "pub-key") {
        throw std::invalid_argument("invalid type format: " + type + ". Expected format: '<key>:<value>'. "
                                                                     "Where <key> = [id|vcard|email|pub-key]");
    }
}
