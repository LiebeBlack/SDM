import sqlite3
db = sqlite3.connect(":memory:")
db.execute("CREATE TABLE downloads (id INTEGER PRIMARY KEY, origin_url TEXT NOT NULL DEFAULT '', referer TEXT NOT NULL DEFAULT '')")
db.execute("INSERT INTO downloads (origin_url, referer) VALUES (?,?)", ("", "https://example.com/page"))
db.execute("UPDATE downloads SET referer=CASE WHEN ?='' THEN referer ELSE ? END WHERE id=1", ("", ""))
print("empty keeps:", db.execute("SELECT referer FROM downloads").fetchone())
db.execute("UPDATE downloads SET referer=CASE WHEN ?='' THEN referer ELSE ? END WHERE id=1", ("https://otra/", "https://otra/"))
print("explicit sets:", db.execute("SELECT referer FROM downloads").fetchone())
try:
    db.execute("INSERT INTO downloads (origin_url) VALUES (?)", (None,))
    print("NULL bind accepted")
except sqlite3.IntegrityError as e:
    print("old NULL bind ->", e)
