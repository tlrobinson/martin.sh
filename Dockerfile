FROM alpine

WORKDIR /app
ADD . /app

EXPOSE 5000
ENV PORT 5000

CMD /app/example.sh
