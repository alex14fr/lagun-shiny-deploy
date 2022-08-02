FROM alpine
RUN wget https://janona.perso.math.cnrs.fr/shiny-rootfs.tar.xz

FROM scratch 
ADD --from=0 rootfs.tar.xz /
EXPOSE 8080 
CMD [ "/entry" ]


