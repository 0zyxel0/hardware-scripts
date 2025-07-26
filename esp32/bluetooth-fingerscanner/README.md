The New Communication Protocol
We will define a fixed-length 7-character string.
Format: SSMMIII
SS: 2-digit Status Code (01-99)
MM: 2-digit Message Code (01-99)
III: 3-digit Data/ID (000-999). It will be 000 if no ID is being sent.

Examples:
0310000 -> Prompt (03), Place Finger (10), No ID (000).
0120002 -> Success (01), Access Granted (20), User ID is 2 (002).