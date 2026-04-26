# cpp-fraud-detection-rinha-2026

![Banner](https://github.com/an-tao/drogon/wiki/images/drogon-white17.jpg)

C++17 Drogon implementation for fraud detection to submit at [Rinha de Backend 2026](https://github.com/zanfranceschi/rinha-de-backend-2026).

## Architecture

```mermaid
graph LR
    client -->|TCP :9999| lb[Drogon LB]
    lb -->|TCP loopback, keepalive| api1[Drogon API 1]
    lb -->|TCP loopback, keepalive| api2[Drogon API 2]
```

Three Drogon-based services share a single Docker image:

| service | role                         | CPU  | mem    |
|---------|------------------------------|------|--------|
| lb      | Drogon reverse-proxy         | 0.20 | 40 MB  |
| api1    | `/fraud-score` + `/ready`    | 0.40 | 155 MB |
| api2    | `/fraud-score` + `/ready`    | 0.40 | 155 MB |

## Build

```bash
docker compose build
docker compose up
```
