FROM ubuntu:22.04
RUN apt-get update && apt-get install -y python3-pip python3-dev libpython3.10-dev gcc netcat
RUN useradd -ms /bin/bash runuser
USER runuser
WORKDIR /app
CMD ["bash"]
