#pragma once

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

// Lightweight QR field parser — no heap allocation in hot paths,
// returns structured data for rendering as a card.
namespace QrCardParser {

// Strip non-printable and non-ASCII characters that the e-ink font
// cannot render (they would appear as '?' on screen).
inline std::string sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (c >= 0x20 && c <= 0x7E) out += static_cast<char>(c);  // printable ASCII
    else if (c == 0x0A || c == 0x0D) out += ' ';              // newline → space
  }
  return out;
}

// URL-decode %XX sequences (only needed for otpauth and mailto params)
inline std::string urlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char hex[3] = {s[i+1], s[i+2], 0};
      int val = 0;
      if (sscanf(hex, "%x", &val) == 1 && val >= 0x20 && val <= 0x7E) {
        out += static_cast<char>(val);
        i += 2;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

struct Field {
    const char* label;  // points to a static string literal (never freed)
    std::string value;
};

struct Result {
    bool success = false;
    const char* format = "UNKNOWN";
    std::string displayTitle;   // primary short description
    std::vector<Field> fields;
    std::string rawText;        // fallback if no format matched
};

// Parse a QR/barcode text string and extract structured fields.
inline Result parse(const std::string& text) {
    Result r;
    if (text.empty()) { r.success = true; r.format = "EMPTY"; return r; }

    // ── Wi‑Fi ───────────────────────────────────────────────────────────────
    if (text.size() >= 5 &&
        (text[0] == 'W' || text[0] == 'w') &&
        (text[1] == 'I' || text[1] == 'i') &&
        (text[2] == 'F' || text[2] == 'f') &&
        (text[3] == 'I' || text[3] == 'i') && text[4] == ':')
    {
        r.format = "WIFI";
        const char* p = text.c_str() + 5;
        const char* end = text.c_str() + text.size();
        std::string ssid, password, security = "nopass";
        bool hidden = false;

        while (p < end) {
            if (*p == ';' || *p == '\0') { ++p; continue; }
            char field = *p;
            if (p + 2 < end && p[1] == ':') {
                p += 2;
                std::string val;
                while (p < end && *p != ';') {
                    if (*p == '\\' && p + 1 < end) { ++p; }  // unescape
                    val += *p++;
                }
                switch (field) {
                    case 'S': case 's': ssid = val; break;
                    case 'P': case 'p': password = val; break;
                    case 'T': case 't': security = val; break;
                    case 'H': case 'h': hidden = (val == "true" || val == "1"); break;
                }
            } else {
                ++p;
            }
        }
        r.success = true;
        r.displayTitle = ssid.empty() ? "Wi-Fi Network" : ssid;
        r.fields.push_back({"SSID", ssid});
        if (!password.empty()) r.fields.push_back({"Password", password});
        r.fields.push_back({"Security", security});
        if (hidden) r.fields.push_back({"Hidden", "Yes"});
        return r;
    }

    // ── vCard ───────────────────────────────────────────────────────────────
    if (text.size() >= 11 && text.compare(0, 11, "BEGIN:VCARD") == 0) {
        r.format = "VCARD";
        r.success = true;
        std::string fn, org, tel, email, lastName, firstName, adr;
        std::vector<std::string> urls;
        const char* p = text.c_str();
        const char* end = p + text.size();

        auto nextLine = [&]() -> std::string {
            std::string line;
            while (p < end && *p != '\n' && *p != '\r') line += *p++;
            while (p < end && (*p == '\n' || *p == '\r')) ++p;
            return line;
        };

        while (p < end) {
            std::string line = nextLine();
            if (line.empty() || line == "END:VCARD") continue;

            if (line.compare(0, 3, "FN:") == 0) {
                fn = line.substr(3);
            } else if (line.compare(0, 4, "ORG:") == 0) {
                org = line.substr(4);
            } else if (line.compare(0, 2, "N:") == 0) {
                std::string n = line.substr(2);
                auto semi = n.find(';');
                if (semi != std::string::npos) {
                    lastName = n.substr(0, semi);
                    firstName = n.substr(semi + 1);
                } else {
                    lastName = n;
                }
            }
            // FIX: Gestisce sia TEL;TYPE=...:value che TEL:value
            else if (line.compare(0, 4, "TEL;") == 0 || line.compare(0, 4, "TEL:") == 0) {
                auto colon = line.find(':');
                if (colon != std::string::npos && tel.empty()) {
                    tel = line.substr(colon + 1);
                }
            } else if (line.compare(0, 6, "EMAIL;") == 0 || line.compare(0, 6, "EMAIL:") == 0) {
                auto colon = line.find(':');
                if (colon != std::string::npos && email.empty()) {
                    email = line.substr(colon + 1);
                }
            } else if (line.compare(0, 4, "URL;") == 0 || line.compare(0, 4, "URL:") == 0) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    urls.push_back(line.substr(colon + 1));
                }
            } else if (line.compare(0, 4, "ADR;") == 0 || line.compare(0, 4, "ADR:") == 0) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string raw = line.substr(colon + 1);
                    // ADR is POBox;Extended;Street;City;Region;Postal;Country
                    // Remove empty segments (;;) and leading/trailing semicolons
                    std::string clean;
                    bool lastSemi = false;
                    std::string current;
                    for (char c : raw) {
                        if (c == ';') {
                            if (!current.empty()) {
                                if (lastSemi) clean += ", ";
                                clean += current;
                                current.clear();
                                lastSemi = true;
                            }
                        } else {
                            current += c;
                            lastSemi = false;
                        }
                    }
                    if (!current.empty()) {
                        if (lastSemi) clean += ", ";
                        clean += current;
                    }
                    if (!clean.empty()) adr = clean;
                }
            }
        }

        std::string full = fn.empty() ? (firstName + " " + lastName) : fn;
        // Trim leading/trailing spaces from assembled name
        while (!full.empty() && full.front() == ' ') full.erase(full.begin());
        while (!full.empty() && full.back() == ' ') full.pop_back();

        r.displayTitle = full.empty() ? "Contact" : full;
        if (!fn.empty()) r.fields.push_back({"Name", fn});
        if (!lastName.empty() || !firstName.empty()) {
            std::string assembled = firstName + " " + lastName;
            while (!assembled.empty() && assembled.front() == ' ') assembled.erase(assembled.begin());
            while (!assembled.empty() && assembled.back() == ' ') assembled.pop_back();
            r.fields.push_back({"Full Name", assembled});
        }
        if (!org.empty()) r.fields.push_back({"Organization", org});
        if (!tel.empty()) r.fields.push_back({"Phone", tel});
        if (!email.empty()) r.fields.push_back({"Email", email});
        for (const auto& u : urls) r.fields.push_back({"URL", u});
        if (!adr.empty()) r.fields.push_back({"Address", adr});
        return r;
    }

    // ── Geo ─────────────────────────────────────────────────────────────────
    if (text.size() >= 4 &&
        (text[0] == 'g' || text[0] == 'G') &&
        (text[1] == 'e' || text[1] == 'E') &&
        (text[2] == 'o' || text[2] == 'O') && text[3] == ':')
    {
        r.format = "GEO";
        std::string data = text.substr(4);
        auto c1 = data.find(',');
        auto c2 = data.find(',', c1 + 1);
        std::string lat = (c1 != std::string::npos) ? data.substr(0, c1) : data;
        std::string lon = (c1 != std::string::npos)
            ? ((c2 != std::string::npos) ? data.substr(c1 + 1, c2 - c1 - 1) : data.substr(c1 + 1))
            : "";
        std::string alt = (c2 != std::string::npos) ? data.substr(c2 + 1) : "";
        r.success = true;
        r.displayTitle = lat + ", " + lon;
        r.fields.push_back({"Latitude", lat});
        r.fields.push_back({"Longitude", lon});
        if (!alt.empty()) r.fields.push_back({"Altitude", alt + " m"});
        return r;
    }

    // ── Mailto ──────────────────────────────────────────────────────────────
    if (text.size() >= 7 &&
        (text.compare(0, 7, "mailto:") == 0 || text.compare(0, 7, "MAILTO:") == 0))
    {
        r.format = "EMAIL";
        std::string addr = text.substr(7);
        auto qPos = addr.find('?');
        std::string email = (qPos != std::string::npos) ? addr.substr(0, qPos) : addr;
        r.displayTitle = email;
        r.fields.push_back({"Email", email});
        if (qPos != std::string::npos) {
            auto subPos = addr.find("subject=", qPos);
            if (subPos != std::string::npos) {
                subPos += 8;
                auto ampEnd = addr.find('&', subPos);
                std::string subj = (ampEnd != std::string::npos)
                    ? addr.substr(subPos, ampEnd - subPos)
                    : addr.substr(subPos);
                if (!subj.empty()) r.fields.push_back({"Subject", subj});
            }
            auto bodyPos = addr.find("body=", qPos);
            if (bodyPos != std::string::npos) {
                bodyPos += 5;
                auto ampEnd = addr.find('&', bodyPos);
                std::string body = (ampEnd != std::string::npos)
                    ? addr.substr(bodyPos, ampEnd - bodyPos)
                    : addr.substr(bodyPos);
                if (!body.empty()) r.fields.push_back({"Body", body});
            }
        }
        r.success = true;
        return r;
    }

    // ── Telephone ───────────────────────────────────────────────────────────
    if (text.size() >= 4 &&
        (text.compare(0, 4, "tel:") == 0 || text.compare(0, 4, "TEL:") == 0))
    {
        r.format = "PHONE";
        std::string number = text.substr(4);
        r.displayTitle = number;
        r.fields.push_back({"Phone", number});
        r.success = true;
        return r;
    }

    // ── SMS ─────────────────────────────────────────────────────────────────
    // FIX: Parsing robusto per entrambi i formati sms: e smsto:
    if ((text.size() >= 6 && (text.compare(0, 6, "smsto:") == 0 || text.compare(0, 6, "SMSTO:") == 0)) ||
        (text.size() >= 4 && (text.compare(0, 4, "sms:") == 0 || text.compare(0, 4, "SMS:") == 0)))
    {
        r.format = "SMS";
        // Trova il primo ':' dopo il prefisso
        size_t prefixLen = (text[3] == ':' || text[3] == ':') ? 4 : 6;
        std::string remainder = text.substr(prefixLen);

        // Per smsto: il formato è smsto:number:body
        // Per sms: il formato è sms:number?body=... oppure sms:number:body
        auto sep = remainder.find(':');
        std::string number, message;
        if (sep != std::string::npos) {
            number = remainder.substr(0, sep);
            message = remainder.substr(sep + 1);
        } else {
            // Fallback: prova con ?body=
            number = remainder;
            auto qPos = remainder.find('?');
            if (qPos != std::string::npos) {
                number = remainder.substr(0, qPos);
                auto bodyPos = remainder.find("body=", qPos);
                if (bodyPos != std::string::npos) {
                    bodyPos += 5;
                    auto ampEnd = remainder.find('&', bodyPos);
                    message = (ampEnd != std::string::npos)
                        ? remainder.substr(bodyPos, ampEnd - bodyPos)
                        : remainder.substr(bodyPos);
                }
            }
        }
        r.displayTitle = number;
        r.fields.push_back({"Number", number});
        if (!message.empty()) r.fields.push_back({"Message", message});
        r.success = true;
        return r;
    }

    // ── OTP Auth (2FA) ──────────────────────────────────────────────────────
    if (text.size() >= 10 &&
        (text.compare(0, 10, "otpauth://") == 0 || text.compare(0, 10, "OTPAUTH://") == 0))
    {
        r.format = "OTPAUTH";
        auto labelStart = text.find('/', 10);
        std::string label = (labelStart != std::string::npos) ? text.substr(labelStart + 1) : "2FA";

        // Decode common percent-encoded chars
        size_t p = 0;
        while ((p = label.find("%20", p)) != std::string::npos) { label.replace(p, 3, " "); ++p; }
        p = 0;
        while ((p = label.find("%40", p)) != std::string::npos) { label.replace(p, 3, "@"); ++p; }

        // Strip query string from label
        auto qPos = label.find('?');
        if (qPos != std::string::npos) label = label.substr(0, qPos);

        auto colon = label.find(':');
        if (colon != std::string::npos) {
            std::string issuer = label.substr(0, colon);
            std::string user = label.substr(colon + 1);
            r.displayTitle = user;
            r.fields.push_back({"Account", user});
            r.fields.push_back({"Issuer", issuer});
        } else {
            r.displayTitle = label;
            r.fields.push_back({"Token", label});
        }

        // Extract algorithm/digits/period from query params if useful
        auto secretPos = text.find("secret=");
        if (secretPos != std::string::npos) {
            r.fields.push_back({"Type", "TOTP/HOTP"});
        }
        r.success = true;
        return r;
    }

    // ── iCal Event ──────────────────────────────────────────────────────────
    if (text.find("BEGIN:VEVENT") != std::string::npos) {
        r.format = "EVENT";
        auto extract = [&](const std::string& field) -> std::string {
            auto pos = text.find(field + ":");
            if (pos == std::string::npos) return "";
            pos += field.length() + 1;
            auto end = text.find_first_of("\r\n", pos);
            return (end != std::string::npos) ? text.substr(pos, end - pos) : text.substr(pos);
        };
        std::string summary = extract("SUMMARY");
        std::string dtstart = extract("DTSTART");
        std::string location = extract("LOCATION");
        std::string desc = extract("DESCRIPTION");
        r.displayTitle = summary.empty() ? "Event" : summary;
        if (!summary.empty()) r.fields.push_back({"Summary", summary});
        if (!dtstart.empty()) r.fields.push_back({"Date", dtstart});
        if (!location.empty()) r.fields.push_back({"Location", location});
        if (!desc.empty()) r.fields.push_back({"Description", desc});
        r.success = true;
        return r;
    }

    // ── MECARD ──────────────────────────────────────────────────────────────
    if (text.size() >= 7 && text.compare(0, 7, "MECARD:") == 0) {
        r.format = "MECARD";
        r.success = true;
        std::string name, phone, email, note, url, adr;
        const char* p = text.c_str() + 7;
        const char* end = text.c_str() + text.size();

        while (p < end) {
            if (*p == ';' || *p == '\0') { ++p; continue; }
            if (p + 2 < end && p[1] == ':') {
                char field = *p;
                p += 2;
                std::string val;
                while (p < end && *p != ';') {
                    if (*p == '\\' && p + 1 < end) { ++p; }
                    val += *p++;
                }
                switch (field) {
                    case 'N': case 'n': name = val; break;
                    case 'T': case 't': phone = val; break;
                    case 'E': case 'e': email = val; break;
                    case 'O': case 'o': note = val; break;
                    case 'U': case 'u': url = val; break;
                    case 'A': case 'a': adr = val; break;
                }
            } else {
                ++p;
            }
        }

        // Convert "Last,First" to "First Last"
        std::string displayName = name;
        auto comma = displayName.find(',');
        if (comma != std::string::npos) {
            displayName = displayName.substr(comma + 1) + " " + displayName.substr(0, comma);
            while (!displayName.empty() && displayName.front() == ' ') displayName.erase(displayName.begin());
            while (!displayName.empty() && displayName.back() == ' ') displayName.pop_back();
        }
        r.displayTitle = displayName.empty() ? "Contact" : displayName;
        if (!name.empty()) r.fields.push_back({"Name", displayName});
        if (!phone.empty()) r.fields.push_back({"Phone", phone});
        if (!email.empty()) r.fields.push_back({"Email", email});
        if (!note.empty()) r.fields.push_back({"Note", note});
        if (!url.empty()) r.fields.push_back({"URL", url});
        if (!adr.empty()) r.fields.push_back({"Address", adr});
        return r;
    }

    // ── Mailto ──────────────────────────────────────────────────────────
    if (text.size() >= 7 && (text.compare(0, 7, "mailto:") == 0 || text.compare(0, 7, "MAILTO:") == 0)) {
      r.format = "EMAIL";
      std::string addr = text.substr(7);
      auto qPos = addr.find('?');
      std::string emailAddr = (qPos != std::string::npos) ? addr.substr(0, qPos) : addr;
      r.displayTitle = emailAddr;
      r.fields.push_back({"Email", emailAddr});
      if (qPos != std::string::npos) {
        auto subPos = addr.find("subject=", qPos);
        if (subPos != std::string::npos) {
          subPos += 8;
          auto end = addr.find('&', subPos);
          std::string subj = (end != std::string::npos) ? addr.substr(subPos, end - subPos) : addr.substr(subPos);
          if (!subj.empty()) r.fields.push_back({"Subject", subj});
        }
      }
      r.success = true;
      return r;
    }

    // ── Telephone ────────────────────────────────────────────────────────
    if (text.size() >= 4 && (text.compare(0, 4, "tel:") == 0 || text.compare(0, 4, "TEL:") == 0)) {
      r.format = "PHONE";
      r.displayTitle = text.substr(4);
      r.fields.push_back({"Phone", text.substr(4)});
      r.success = true;
      return r;
    }

    // ── SMS ──────────────────────────────────────────────────────────────
    if ((text.size() >= 4 && (text.compare(0, 4, "sms:") == 0 || text.compare(0, 4, "SMS:") == 0)) ||
        (text.size() >= 6 && (text.compare(0, 6, "smsto:") == 0 || text.compare(0, 6, "SMSTO:") == 0))) {
      r.format = "SMS";
      auto colon = text.find(':');
      std::string body = text.substr(colon + 1);
      auto c2 = body.find(':');
      std::string number = (c2 != std::string::npos) ? body.substr(0, c2) : body;
      r.displayTitle = number;
      r.fields.push_back({"Number", number});
      if (c2 != std::string::npos) r.fields.push_back({"Message", body.substr(c2 + 1)});
      r.success = true;
      return r;
    }

    // ── OTP Auth (2FA) ───────────────────────────────────────────────────
    if (text.size() >= 10 && (text.compare(0, 10, "otpauth://") == 0 || text.compare(0, 10, "OTPAUTH://") == 0)) {
      r.format = "OTPAUTH";
      auto labelStart = text.find('/', 10);
      std::string label = (labelStart != std::string::npos) ? text.substr(labelStart + 1) : "2FA";
      size_t p = 0;
      while ((p = label.find("%20", p)) != std::string::npos) { label.replace(p, 3, " "); ++p; }
      auto colon = label.find(':');
      if (colon != std::string::npos) {
        std::string issuer = label.substr(0, colon);
        std::string user = label.substr(colon + 1);
        r.displayTitle = user;
        r.fields.push_back({"Account", user});
        r.fields.push_back({"Issuer", issuer});
      } else {
        r.displayTitle = label;
        r.fields.push_back({"Token", label});
      }
      r.success = true;
      return r;
    }

    // ── iCal Event ───────────────────────────────────────────────────────
    // Handles: BEGIN:VEVENT … END:VEVENT (possibly embedded in VCALENDAR)
    if (text.find("BEGIN:VEVENT") != std::string::npos) {
      r.format = "EVENT";
      // Extract property value: field may be "PROPERTY:value" or "PROPERTY;PARAM=val:value"
      auto extract = [&](const std::string& field) -> std::string {
        // Search case-insensitively
        std::string upper = field;
        for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        std::string upperText = text;
        for (auto& c : upperText) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        auto pos = upperText.find(upper);
        if (pos == std::string::npos) return "";
        // Skip past the field name, then skip any parameters (;…:) until ':'
        pos += field.length();
        while (pos < text.size() && text[pos] == ';') {
          // skip parameter name and value until next ';' or ':'
          pos = text.find_first_of(";:", pos);
          if (pos == std::string::npos || text[pos] == ':') break;
          ++pos; // skip ';'
        }
        if (pos == std::string::npos || text[pos] != ':') return "";
        ++pos; // skip ':'
        // Value until end of line (handle \r\n, \n, or folding: \r\n space)
        auto end = text.find_first_of("\r\n", pos);
        std::string value = (end != std::string::npos) ? text.substr(pos, end - pos) : text.substr(pos);
        return value;
      };
      std::string summary = extract("SUMMARY");
      std::string dtstart = extract("DTSTART");
      std::string dtend = extract("DTEND");
      std::string location = extract("LOCATION");
      std::string description = extract("DESCRIPTION");
      r.displayTitle = summary.empty() ? "Event" : summary;
      if (!summary.empty()) r.fields.push_back({"Summary", summary});
      if (!dtstart.empty()) r.fields.push_back({"Start", dtstart});
      if (!dtend.empty()) r.fields.push_back({"End", dtend});
      if (!location.empty()) r.fields.push_back({"Location", location});
      if (!description.empty()) r.fields.push_back({"Description", description});
      r.success = true;
      return r;
    }

    // ── URL ─────────────────────────────────────────────────────────────────
    // FIX: Boundary check corretto per https
    if ((text.size() >= 7 && text.compare(0, 7, "http://") == 0) ||
        (text.size() >= 8 && text.compare(0, 8, "https://") == 0))
    {
        r.format = "URL";
        r.success = true;
        auto start = text.find("://") + 3;
        auto slash = text.find('/', start);
        std::string domain = (slash != std::string::npos)
            ? text.substr(start, slash - start)
            : text.substr(start);
        // Remove www. prefix for cleaner display
        if (domain.compare(0, 4, "www.") == 0) domain = domain.substr(4);
        r.displayTitle = domain;
        r.fields.push_back({"URL", text});
        r.fields.push_back({"Domain", domain});
        return r;
    }

    // ── Unknown / Plain Text ────────────────────────────────────────────────
    r.success = true;
    r.format = "TEXT";
    r.rawText = text;
    r.displayTitle = text.size() > 40 ? text.substr(0, 37) + "..." : text;
    return r;
}

}  // namespace QrCardParser