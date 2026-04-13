// Copy to google_secrets.h (keep out of git; see repo README), or run from repo root:
//   make oauth-setup
//
// Calendars shown are those for the Google account you authorize (e.g. john@oram.ca):
// use Google Cloud Console → APIs & Services → Credentials → OAuth client ID (Desktop),
// scope https://www.googleapis.com/auth/calendar.readonly, then obtain a refresh token
// (OAuth 2.0 Playground with your client id/secret, or a one-time local OAuth run).
// If token refresh returns HTTP 401 "unauthorized_client", the refresh token was not issued
// for this exact client id/secret (or the client type/secret is wrong) — re-authorize with
// the same Desktop client and update all three defines together.

#pragma once

#define GOOGLE_CLIENT_ID ""
#define GOOGLE_CLIENT_SECRET ""
#define GOOGLE_REFRESH_TOKEN ""
