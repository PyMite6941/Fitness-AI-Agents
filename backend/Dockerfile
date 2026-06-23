FROM python:3.11
WORKDIR /app
COPY requirements-deploy.txt .
RUN pip install --no-cache-dir --default-timeout=300 -r requirements-deploy.txt
COPY . .
CMD exec uvicorn main:app --host 0.0.0.0 --port ${PORT:-7860}