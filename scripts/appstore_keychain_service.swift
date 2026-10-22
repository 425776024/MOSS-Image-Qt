// Work around altool versions that store by label but retrieve by service.
// Updates metadata only: never reads or prints the credential data.
import Foundation
import Security
if CommandLine.arguments.count != 3 { exit(2) }
let item=CommandLine.arguments[1], account=CommandLine.arguments[2]
let query:[CFString:Any] = [kSecClass:kSecClassGenericPassword,kSecAttrLabel:item,kSecAttrAccount:account]
let changes:[CFString:Any] = [kSecAttrService:item]
let status=SecItemUpdate(query as CFDictionary,changes as CFDictionary)
if status != errSecSuccess { print("Keychain metadata update failed:",status); exit(1) }
