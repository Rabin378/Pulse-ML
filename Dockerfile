FROM gcc:13-bookworm
WORKDIR /app
COPY . .
RUN g++ -std=c++17 -O3 -fstack-protector-strong src/main.cpp -pthread -o pulseml
USER nobody
EXPOSE 8080
CMD ["./pulseml"]
