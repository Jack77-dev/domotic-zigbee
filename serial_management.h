/* serial management
  01/09/2026, version 1.2 : correction bug sur test saturation, ajout delai < 1sec
  13/07/2026, version 1.1
    ajout filtrage/cleaning du message recu
 format :       D;25;DEST;EMET;Commande;F
                D is mandatory
                ; is the members separator (between each members)
                25 = total message length
                DEST : target device
                EMET : source device
                Command : text payload
                F is mandatory

  ESP32-C6 serial pin configuration :
  Serial1.begin(115200, SERIAL_8N1, 6, 7);  // Configure Serial1 on pins TX=D6 and RX=D7
  Serial2.begin(115200, SERIAL_8N1, 4, 5);  // And configure Serial2 on pins RX=D4, TX=D5
*/
long nb_read = 0; String last_msg_read=""; long last_read_time = 0; long nb_appels = 0; long nb_write = 0; 
/* doc : serial_read(int param)
  module de lecture des ports serie, peut lire serial, serial1, serial2 selon le parametre recu.
  - la lecture se fait jusqu'au caractere NewLine
  - seuls les caracteres imprimables (isprint) sont retenus, les autres sont ignores
  - si le meme message revient 10 fois de suite, il est ignore
  - le message lu doit respecter format "D;LNG;DST;SRC;Comaande;F", sinon il est ignore
  - 
*/
String serial_read(int param) {
  String message_lu; int erreur=0; message_lu = ""; String message_brut;
  nb_appels++;
  //
  switch (param) {
    case 0 : message_brut = Serial.readStringUntil('\n');  break;
    case 1 : message_brut = Serial1.readStringUntil('\n'); break;
    case 2 : message_brut = Serial2.readStringUntil('\n'); break;
    default : Serial.println("param invalide : "+param);
  }
  // filtrage/cleaning du message_lu
  size_t length = message_brut.length();
  for( size_t i=0; i<length; i++ ) {
    if ( isprint( message_brut[i] ) ) {
            message_lu=message_lu+message_brut[i];
        }
  }
  //
  if (xTrace) { Serial.printf("nbAppels=%d, reception(%d)=",nb_appels,param);Serial.println(message_lu); }
  // read saturation checking
  if ( last_msg_read==message_lu ) { nb_read++; }
  else { nb_read=0; last_msg_read=message_lu; last_read_time=millis(); }
  if ( nb_read>10 && (millis()-last_read_time)<1000 ) { Serial.println("Error : serial saturation, message looping (>10)"); return(""); }
  else { nb_read=0; last_read_time=millis(); }
  
  // decoder le message lu format "D;LNG;DST;SRC;Comaande;F"
  long lg = message_lu.length(); char sep = message_lu[1];
  long i; String DST=""; String SRC=""; String LNG=""; String CMD=""; long SrcStart=0;
  for (i=2;i<lg & message_lu[i] != sep; i++){ LNG = LNG+message_lu[i]; }
  for (i=i+1;i<lg & message_lu[i] != sep; i++){ DST = DST+message_lu[i]; } SrcStart=i+1;
  for (i=i+1;i<lg & message_lu[i] != sep; i++){ SRC = SRC+message_lu[i]; }
  for (i=i+1;i<lg & message_lu[i] != sep; i++){ CMD = CMD+message_lu[i]; }
  if (xTrace) { Serial.println(lg);Serial.println(LNG);Serial.println(DST);Serial.println(SRC); Serial.println(CMD); }

  // verifier coherence du message lu
  if (message_lu[0]!='D' || message_lu[lg-1]!='F' || DST=="" || SRC=="" || LNG==""){ erreur+=1; Serial.println("Error : Invalid message structure"); }
  if (lg != LNG.toInt()) { erreur+=1; Serial.println("Error : Invalid message length"); }      // verifier coherence longueur 
  if (erreur!=0){ return("");}

  // serial network management
  if ( SRC != MySerialName & DST != MySerialName ) {
    Serial2.print(message_lu+'\n');   // propager le message sur serial network
    if (xTrace) { Serial.print("Propagation du message lu : "); Serial.println(message_lu); }
    }
  if ( DST == MySerialName || DST == "*" ) { return (message_lu.substring(SrcStart,lg-2)); }       // consommer le message lu "SRC;ACTION"
  else { return (""); }
  //return (message_lu);
}

void serial_write(String message) {
  nb_write++;
  // format attendu : ;DST;Commande
  if (xTrace) { Serial.print("Ecriture=");Serial.println(message); }
  long lg = message.length(); if (lg<2){ return; }
  char sep = message[0];
  long i; String DST=""; String SRC=""; String LNG=""; long SrcStart=0;
  for (i=1;i<lg & message[i] != sep; i++){ DST = DST+message[i]; } SrcStart=i+1;
  String Texte = sep+DST+sep+MySerialName+sep+message.substring(SrcStart)+sep+"F";
  if (xTrace) { Serial.printf("N° write: %d, ",nb_write); Serial.println(Texte); }
  // message checking
  if ( DST=="" ){ Serial.println("Error : DTS is null"); return;}
  long lgOut = (Texte+"D;00").length();
  if ( lgOut>99 ) { lgOut+=1; }
  Texte = "D;"+String(lgOut)+Texte+'\n';
  if (xTrace) { Serial.print("Output=");Serial.print(Texte); }
  Serial2.print(Texte);Serial2.flush();
}