FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    CHIBI_HOST=0.0.0.0 \
    WHISPER_MODEL=base \
    HF_HOME=/opt/chibi-models

RUN apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg libopus0 ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Fetch the ASR weights at image-build time to avoid runtime cold-start downloads.
RUN python -c "from faster_whisper import WhisperModel; WhisperModel('base', device='cpu', compute_type='int8')"

COPY chibi_server.py .
RUN useradd --system --create-home --uid 10001 chibi && \
    chown -R chibi:chibi /app /opt/chibi-models
USER chibi

EXPOSE 8080
CMD ["python", "chibi_server.py"]
