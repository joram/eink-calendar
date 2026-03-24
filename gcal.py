import os
import datetime
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from google.auth.transport.requests import Request
from googleapiclient.discovery import build

SCOPES = ['https://www.googleapis.com/auth/calendar.readonly']
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CREDENTIALS_FILE = os.path.join(BASE_DIR, 'credentials.json')
TOKEN_FILE = os.path.join(BASE_DIR, 'token.json')


def get_service():
    creds = None
    if os.path.exists(TOKEN_FILE):
        creds = Credentials.from_authorized_user_file(TOKEN_FILE, SCOPES)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            flow = InstalledAppFlow.from_client_secrets_file(CREDENTIALS_FILE, SCOPES)
            # Headless-friendly: prints URL, waits for redirect to localhost:8080
            creds = flow.run_local_server(port=8080, open_browser=False)
        with open(TOKEN_FILE, 'w') as f:
            f.write(creds.to_json())
    return build('calendar', 'v3', credentials=creds)


def get_events(days=3):
    """Return events for today through today+days, grouped by local date.

    Returns: dict mapping datetime.date -> list of {'time': str, 'title': str}
    """
    service = get_service()
    today = datetime.date.today()
    time_min = datetime.datetime.combine(today, datetime.time.min).astimezone()
    time_max = datetime.datetime.combine(today + datetime.timedelta(days=days), datetime.time.min).astimezone()

    items = service.events().list(
        calendarId='primary',
        timeMin=time_min.isoformat(),
        timeMax=time_max.isoformat(),
        maxResults=50,
        singleEvents=True,
        orderBy='startTime',
    ).execute().get('items', [])

    grouped = {}
    for event in items:
        start = event['start']
        if 'dateTime' in start:
            dt = datetime.datetime.fromisoformat(start['dateTime']).astimezone()
            date_key = dt.date()
            time_str = dt.strftime('%-I:%M %p')
        else:
            date_key = datetime.date.fromisoformat(start['date'])
            time_str = 'All day'
        grouped.setdefault(date_key, []).append({
            'time': time_str,
            'title': event.get('summary', '(No title)'),
        })

    return grouped
