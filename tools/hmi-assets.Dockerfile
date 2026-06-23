FROM python:3.13.5-slim-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir Pillow==11.3.0

WORKDIR /src
ENTRYPOINT ["python", "/src/tools/generate_hmi_assets.py"]
