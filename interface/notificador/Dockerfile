FROM python:3.12-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY notificador.py .

# -u: unbuffered, so every alert shows up in "docker compose logs" right away.
CMD ["python", "-u", "notificador.py"]
