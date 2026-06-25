FROM node:20-slim
WORKDIR /app
COPY package.json ./
RUN npm install --no-audit --no-fund
COPY *.ts ./
ENV PORT=38500 RATING_FILE=/data/elo.json
EXPOSE 38500
CMD ["npx", "tsx", "relay.ts"]
