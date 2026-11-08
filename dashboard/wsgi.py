"""The dashboard for a WSGI server, as the home server runs it (deploy/systemd/algo-trader-dashboard.service):

    gunicorn --workers 1 --threads 8 --bind 0.0.0.0:8050 "dashboard.wsgi:server"

One worker only: the backtests the dashboard starts are tracked in its memory.
On your own computer, python -m dashboard (or ./run_dashboard.sh) is still the way to run it.
"""

from .app import create_app
from .project import Project

server = create_app(Project.find()).server
